#include "system.hpp"
#include "gameplay/skills/mana_payment.hpp"
#include "attribute_projection.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/combat/geometry.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "core/random.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/event_frame.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "gameplay/effects/queries.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <span>
#include <set>
#include <stdexcept>
#include <optional>
#include <cmath>
#include <exception>
#include <limits>
namespace d2x::server::effects {
struct ItemAuraCast {System::PaladinCycle cycle;bool complete{};};
DomainResult<> System::itemAura(const ActorContext &actor,int skill,int rank,std::shared_ptr<ItemAuraCast> &execution) {
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs() || !caster->programs()->auras.contains(skill)) return {DomainStatus::Unavailable,{}};
    if(!execution) {
        execution=std::make_shared<ItemAuraCast>();
        execution->cycle.skill=skill;execution->cycle.rank=rank;execution->cycle.area=actor.area;
    }
    if(!execution->complete) {
        const auto status=caster->player?
            advanceAuraCycle(actor,*caster->player,caster->programs()->auras.at(skill).spec,execution->cycle,actor.tick):
            advanceUnitAuraCycle(actor,actor.actor,execution->cycle,actor.tick);
        if(status==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        execution->complete=true;
    }
    // ItemEffect invokes Do once; it does not select a right-hand aura or
    // install another periodic-skills timer.
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::paladinAura(const ActorContext &actor,const AuraDefinition &a,EntityId id,uint64_t duration,bool owner,bool healing,EntityId emitter,bool *committedAffected) {
    const auto *caster=ports_.players.find(actor.player);
    const auto *emittingUnit=emitter?ports_.monsters.find(emitter):nullptr;
    const auto *schema=emittingUnit?emittingUnit->rule.stats.get():caster && caster->rules.skills?caster->rules.skills->stats.get():nullptr;
    const auto *formulas=emittingUnit?emittingUnit->rule.formulas.get():caster && caster->rules.skills?caster->rules.skills->formulas.get():nullptr;
    if(!schema || !formulas) return {DomainStatus::Unavailable,{}};
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto recipientView=participants.find(id);
    const auto source=emitter?participants.find(emitter):combat::ParticipantView{caster,nullptr};
    const bool pd2=emittingUnit?(ports_.monsters.skillPrograms() && ports_.monsters.skillPrograms()->pd2):caster && caster->rules.skills && caster->rules.skills->pd2;
    const RegionId region=recipientView.player?recipientView.player->area:recipientView.monster?recipientView.monster->area:actor.area;
    const RegionId sourceRegion=source.player?source.player->area:source.monster?source.monster->area:actor.area;
    if(region!=sourceRegion && !ports_.areas.nearby(sourceRegion,source.position(),region,recipientView.position())) return {DomainStatus::InvalidActor,{}};
    if(!recipientView.aliveIn(region) || (id!=source.id() &&
       !(a.hostile?participants.canHarm(source,recipientView):participants.auraAligned(source,recipientView))))
        return {DomainStatus::InvalidActor,{}};
    CombatEffectSpec spec;spec.state=owner?a.ownerState:a.state;
    spec.source={CombatEffectSource::Skill,emitter?emitter:actor.actor,a.skill,a.rank};spec.duration=duration;spec.stacking=EffectStacking::AuraLevel;
    spec.modifiers=owner?auraOwnerModifiers(a):a.modifiers;
    if(a.ownerWrites || a.targetWrites) {
        spec.nativeState=spec.state.id>=0;
        spec.refreshPassivesOnRemoval=a.serverDoFunction!=81 || !owner;
        if(owner) {
            spec.nativeRemoval=a.serverDoFunction==81?NativeStateRemoval::HolyFreeze:NativeStateRemoval::BasicAura;
            spec.clearStateOnRemoval=a.serverDoFunction==81?
                (emittingUnit?emittingUnit->rule.shatterState.id:caster->rules.skills->shatter.id):
                (emittingUnit?emittingUnit->rule.noManaRegenState:caster->rules.character->noManaRegenState);
        }
    }
    auto writes=owner?a.ownerWrites:a.targetWrites;
    if(!owner && a.serverDoFunction==81 && recipientView.monster && recipientView.monster->rule.coldEffect>=0)
        return {DomainStatus::Unavailable,{}};
    if(!owner && writes && writes->empty() && (a.serverDoFunction==66 || a.serverDoFunction==81))
        return {DomainStatus::Applied,std::monostate{}};
    size_t callbackWrites=writes?(owner?a.ownerCallbackWrites:writes->size()):0;
    bool affected=false;
    if(writes) {
        if(callbackWrites>writes->size()) throw std::invalid_argument("Invalid native aura callback boundary");
        const auto &rules=*schema;
        std::vector<StatValue> adjusted;
        const auto *monster=recipientView.monster;
        if(a.skill==114 && !owner && monster && monster->rule.coldEffect>=0)
            return {DomainStatus::Unavailable,{}};
        for(size_t index=0;index<callbackWrites;++index) {
            auto write=(*writes)[index];
            const auto &name=rules.definitions.at(write.key.id).name;
            // BasicAura's resistance helper reads Base Stats, without temporary
            // aura/curse contributions; players and hirelings bypass dilution.
            if(monster && !monster->hireling && write.value<0) {
                const std::pair<std::string_view,size_t> resistanceStats[]{
                    {"damageresist",0},{"magicresist",1},{"fireresist",2},
                    {"lightresist",3},{"coldresist",4},{"poisonresist",5}};
                for(const auto &[stat,channel]:resistanceStats) if(name==stat) {
                    const int base=monster->nativeStats && monster->nativeStats->sources?
                        baseStatValue(rules,*monster->nativeStats->sources,write.key):monster->rule.resistances[channel];
                    if(base>=100) write.value/=pd2?2:5;
                    break;
                }
            }
            if(!write.value) continue;
            if(name=="velocitypercent" || name=="attackrate")
                write.value=std::max(write.value,monster?monster->rule.coldEffect:-50);
            adjusted.push_back(write);
            if(name=="attackrate")
                adjusted.emplace_back(StatKey{rules.names.at("other_animrate"),0},write.value);
        }
        const size_t nativeCallbacks=adjusted.size();
        adjusted.insert(adjusted.end(),writes->begin()+callbackWrites,writes->end());
        callbackWrites=nativeCallbacks;writes=std::move(adjusted);
        StatSnapshot projection;
        for(size_t index=0;index<writes->size();++index) {
            const auto &write=(*writes)[index];
            const auto &rule=rules.definitions.at(write.key.id);
            if(index<callbackWrites && (rule.direct || rule.name=="item_poisonlengthresist")) continue;
            writeNativeBaseStat(projection.values,rules,write.key,write.value);
        }
        spec.modifiers=nativeCharacterModifiers(projection,rules);
    }
    const PlayerState *recipient=recipientView.player;
    if(recipient && writes) {
        if(!recipient->entered || recipient->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
        std::vector<DomainFact> facts;
        const ActorContext target{recipient->player,id,region,ports_.areas.at(region).generation,0,actor.tick};
        const transactions::InitialEffectMutation apply=[&](CombatEffectSet &states,const attributes::AuraSourceObserver &observe,const attributes::AuraSourceReader &read,PlayerPoisonCandidate &poison) {
            const bool wasActive=states.hasState(spec.state.id,actor.tick);
            std::optional<std::vector<StatValue>> previous;
            for(const auto &effect:states.entries()) if(effect.spec.state.id==spec.state.id) {previous=nativeEffectStats(*schema,effect.spec);break;}
            const auto publish=[&](std::optional<ResourceValues> resources={},std::vector<int> withdrawn={}) {
                auto projection=projectEffectAttributes(states,actor.tick);
                observe({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),resources,std::move(withdrawn)});
            };
            const auto procedure=[&](StatValue write)->std::optional<bool> {
                const auto &rule=schema->definitions.at(write.key.id);
                if(rule.name=="item_poisonlengthresist") {
                    const bool changed=shortenAuraHarmfulDurations(states,poison.poison,actor.tick,write.value);
                    poison.changed|=changed;
                    return changed;
                }
                if(!rule.direct) return {};
                const auto snapshot=read();
                ResourceValues resources{Fixed8::fromRaw(snapshot.currentResources.at({schema->names.at("hitpoints"),0})),
                    Fixed8::fromRaw(snapshot.currentResources.at({schema->names.at("mana"),0})),
                    Fixed8::fromRaw(snapshot.currentResources.at({schema->names.at("stamina"),0}))};
                auto *value=rule.name=="hitpoints"?&resources.life:rule.name=="mana"?&resources.mana:rule.name=="stamina"?&resources.stamina:nullptr;
                if(!value) throw std::runtime_error("Native player aura requires its direct base-stat writer");
                const auto maximum=rule.maximumStat?std::optional<int32_t>{snapshot.stats.get({*rule.maximumStat,0})}:std::nullopt;
                const auto next=nativeAuraDirectStatValue(value->raw(),write.value,maximum);
                if(next==value->raw()) return false;
                *value=Fixed8::fromRaw(next);publish(resources);return true;
            };
            if(spec.state.id<0) return;
            const auto application=applyNativeAuraState(states,spec,writes,callbackWrites,*schema,actor.tick,[&]{publish();},procedure,&affected);
            if(!application.accepted) return;
            const auto passiveState=formulas->skills.at(a.skill).passiveState;
            if(passiveState>0) publish({},std::vector<int>{passiveState});
            for(const auto &removed:application.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
                facts.emplace_back(StateFact{id,0,region,removed.effect.spec.state.id,false});
            const auto *installed=states.find(application.handle);
            if(!installed) throw std::logic_error("Missing native player aura list");
            auto values=nativeEffectStats(*schema,installed->spec);
            if(!wasActive || !previous || values!=*previous) facts.emplace_back(StateFact{id,0,region,spec.state.id,true,std::move(values)});
        };
        transactions::CharacterEdit edit{target,recipient->inventoryRevision,recipient->characterRevision,recipient->persistent.player};
        auto plan=ports_.transactions.prepare(std::move(edit),nullptr,apply);if(!plan) return {plan.status,{}};
        auto &preparedEdit=std::get<transactions::CharacterEdit>(plan.value->change);
        preparedEdit.publicFacts.insert(preparedEdit.publicFacts.end(),facts.begin(),facts.end());
        const auto result=ports_.transactions.commit(std::move(*plan.value));
        if(result && committedAffected) *committedAffected=affected;
        return result;
    }
    if(recipient) {
        if(!recipient->entered || recipient->area!=region || recipient->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
        auto next=state_;auto &r=next.players[id];
        PlayerResourceTrace resources(*recipient,actor.tick,{},ports_.areas.statTime(recipient->area));
        const auto observe=[&]{resources.sample(r.states,true);};
        const auto procedure=[&](StatValue write)->std::optional<bool> {
            const auto &rules=*schema;
            const auto &rule=rules.definitions.at(write.key.id);
            if(rule.name=="item_poisonlengthresist") {
                bool changed=r.states.shortenCurableCurses(actor.tick,write.value);
                if(r.poison && r.poison->until>actor.tick) {
                    r.poison->until=nativeAuraDurationTick(actor.tick,r.poison->until,write.value);
                    for(const auto &effect:r.states.entries()) if(effect.spec.state.id==r.poison->damage.state) {
                        r.states.setExpiresAt(effect.handle,r.poison->until,actor.tick);break;
                    }
                    changed=true;
                }
                return changed;
            }
            if(!rule.direct) return {};
            auto record=resources.record();
            Fixed8 *value=rule.name=="hitpoints"?&record.hp:rule.name=="mana"?&record.mana:
                rule.name=="stamina"?&record.stamina:nullptr;
            if(!value) throw std::runtime_error("Native player aura direct stat requires a unit base writer: "+rule.name);
            std::optional<int32_t> maximum;
            if(rule.maximumStat) maximum=resources.totals().nativeStats.get({*rule.maximumStat,0});
            const int32_t next=nativeAuraDirectStatValue(value->raw(),write.value,maximum);
            if(next==value->raw()) return false;
            *value=Fixed8::fromRaw(next);
            resources.seedResources(record.hp,record.mana,record.stamina);observe();
            return true;
        };
        std::optional<EffectApplication> application;
        if(spec.state.id>=0) {
            application=applyNativeAuraState(r.states,spec,writes,callbackWrites,*schema,actor.tick,observe,procedure,&affected);
            if(!application->accepted) return {DomainStatus::Applied,std::monostate{}};
            if(writes) resources.withdrawPassiveState(formulas->skills.at(a.skill).passiveState,r.states);
        }
        if(!writes && a.harmfulDurationPercent<100) {
            r.states.shortenCurableCurses(actor.tick,a.harmfulDurationPercent);
            if(r.poison && r.poison->until>actor.tick) {
                r.poison->until=actor.tick+(r.poison->until-actor.tick)*uint64_t(std::max(0,a.harmfulDurationPercent))/100;
                std::optional<CombatEffectSpec> poison;
                for(const auto &effect:r.states.entries()) if(effect.spec.state.id==r.poison->damage.state) {poison=effect.spec;break;}
                if(poison) {poison->duration=std::max<uint64_t>(1,r.poison->until-actor.tick);r.states.apply(std::move(*poison),actor.tick,observe);}
            }
        }
        const ActorContext target{recipient->player,recipient->actor,recipient->area,ports_.areas.at(recipient->area).generation,0,actor.tick};
        transactions::CharacterEdit edit{target,recipient->inventoryRevision,recipient->characterRevision,resources.record()};
        if(!writes && healing && a.lifePerPulse>0) edit.player.hp=std::min(resources.totals().character.maxLifeFixed,edit.player.hp+a.lifePerPulse);
        if(!writes && owner && a.manaPerPulse>0 && !emitter) {
            const int state=caster->rules.character->noManaRegenState;
            resources.seedResources(edit.player.hp,edit.player.mana,edit.player.stamina);
            r.states.removeState(state,observe);edit.player=resources.record();
            if(healing) {
                if(edit.player.mana<a.manaPerPulse && !r.states.hasState(caster->rules.character->bloodManaState,actor.tick)) return {DomainStatus::Unavailable,{}};
                edit.player.mana-=a.manaPerPulse;
                edit.skillPayment=transactions::SkillManaPayment{a.manaPerPulse,a.skill,true};
                resources.seedResources(edit.player.hp,edit.player.mana,edit.player.stamina);
                CombatEffectSpec stop;stop.state.id=state;stop.source=spec.source;stop.duration=duration;r.states.apply(std::move(stop),actor.tick,observe);
                edit.player=resources.record();
            }
        }
        edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();
        edit.transient=projectEffectAttributes(r.states,actor.tick);
        if(application) {
            for(const auto &removed:application->removed) if(!r.states.hasState(removed.effect.spec.state.id,actor.tick))
                edit.publicFacts.emplace_back(StateFact{id,0,region,removed.effect.spec.state.id,false});
            const auto *installed=r.states.find(application->handle);
            if(!installed) throw std::logic_error("Missing installed native player aura state");
            auto stats=nativeEffectStats(*schema,installed->spec);
            std::optional<std::vector<StatValue>> previous;
            if(const auto current=state_.players.find(id);current!=state_.players.end())
                for(const auto &effect:current->second.states.entries()) if(effect.spec.state.id==spec.state.id && effect.activeAt(actor.tick)) {
                    previous=nativeEffectStats(*schema,effect.spec);break;
                }
            if(!previous || stats!=*previous)
                edit.publicFacts.emplace_back(StateFact{id,0,region,spec.state.id,true,std::move(stats)});
        }
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto result=ports_.transactions.commit(std::move(*plan.value));
        if(result) {publishPlayers(next.players);if(committedAffected) *committedAffected=affected;}
        return result;
    }
    const auto *m=recipientView.monster;if(!m) return {DomainStatus::InvalidActor,{}};
    if(writes && !m->rule.stateTarget.acceptsCurseList()) return {DomainStatus::Applied,std::monostate{}};
    if(!writes && a.skill==114) {
        if(m->rule.coldEffect>=0) return {DomainStatus::Unavailable,{}};
        spec.modifiers.velocityPercent=std::max(spec.modifiers.velocityPercent,m->rule.coldEffect);
        spec.modifiers.combat.attackRate=std::max(spec.modifiers.combat.attackRate,m->rule.coldEffect);
        spec.modifiers.otherAnimationRate=spec.modifiers.combat.attackRate;
    }
    if(!writes && a.skill==123) {
        auto existing=unitModifiers(id,actor.tick);
        const auto previous=stateModifiers(id,spec.state.id,actor.tick);
        existing.fireResist-=previous.fireResist;existing.lightningResist-=previous.lightningResist;existing.coldResist-=previous.coldResist;
        if(m->rule.resistances[2]+existing.fireResist>=100) spec.modifiers.fireResist/=5;
        if(m->rule.resistances[3]+existing.lightningResist>=100) spec.modifiers.lightningResist/=5;
        if(m->rule.resistances[4]+existing.coldResist>=100) spec.modifiers.coldResist/=5;
    }
    auto next=units_;auto &unit=next[id];unit.area=region;unit.converted=bool(m->conversion);
    std::optional<MonsterResourceTrace> resources;
    if(m->nativeStats && m->nativeStats->sources && m->rule.stats) resources.emplace(*m,actor.tick,&unit.states);
    auto poisonUntil=m->poison?m->poison->until:0;
    std::vector<int> poisonPercentages;
    const auto procedure=[&](StatValue write)->std::optional<bool> {
        const auto &rules=*schema;
        const auto &rule=rules.definitions.at(write.key.id);
        if(rule.name=="item_poisonlengthresist") {
            bool changed=unit.states.shortenCurableCurses(actor.tick,write.value);
            if(m->poison && poisonUntil>actor.tick) {
                poisonUntil=nativeAuraDurationTick(actor.tick,poisonUntil,write.value);
                poisonPercentages.push_back(write.value);changed=true;
            }
            return changed;
        }
        if(!rule.direct) return {};
        if(!resources) throw std::runtime_error("Missing native monster aura direct stat sources");
        auto sources=combatEffectStatSources(unit.states.entries(),actor.tick,true);
        const auto values=resources->values(sources);
        const int32_t current=rule.name=="hitpoints"?resources->read().life.raw():values.get(write.key);
        std::optional<int32_t> maximum;
        if(rule.maximumStat) maximum=values.get({*rule.maximumStat,0});
        const int32_t value=nativeAuraDirectStatValue(current,write.value,maximum);
        if(value==current) return false;
        if(rule.name=="hitpoints") resources->seedLife(Fixed8::fromRaw(value));
        else resources->writeBaseStat(rule.name,value,sources);
        return true;
    };
    std::vector<DomainFact> facts;
    if(spec.state.id>=0) {
        const auto application=applyNativeAuraState(unit.states,spec,writes,callbackWrites,*schema,actor.tick,
            [&]{if(resources) resources->sample(unit.states,true);},procedure,&affected);
        if(!application.accepted) return {DomainStatus::Applied,std::monostate{}};
        if(writes && resources) resources->withdrawPassiveState(formulas->skills.at(a.skill).passiveState,unit.states);
        const auto *installed=unit.states.find(application.handle);
        if(!installed) throw std::logic_error("Missing installed native aura state");
        auto stats=nativeEffectStats(*schema,installed->spec);
        for(const auto &removed:application.removed) if(!unit.states.hasState(removed.effect.spec.state.id,actor.tick))
            facts.emplace_back(StateFact{id,1,region,removed.effect.spec.state.id,false});
        if(unit.nativeStats[spec.state.id]!=stats || !unitStates(id,actor.tick).contains(spec.state.id)) {
            facts.emplace_back(StateFact{id,1,region,spec.state.id,true,stats});
        }
        unit.nativeStats[spec.state.id]=std::move(stats);
    }
    if(!writes && a.harmfulDurationPercent<100) unit.states.shortenCurableCurses(actor.tick,a.harmfulDurationPercent);
    if(resources) {
        resources->sample(unit.states);
        if(m->poison && !poisonPercentages.empty()) {
            auto poison=m->poison;poison->until=poisonUntil;resources->seedPoison(std::move(poison));
        }
        if(!ports_.monsters.canCommitLife(resources->read())) return {DomainStatus::Conflict,{}};
    }
    auto timers=ports_.frameEvents.prepareEdit({FrameEventUnit::Monster,id},FrameEventType::RemoveState,unit.states.expirationEvents());
    if(!facts.empty() && !ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},region},std::move(facts)})) return {DomainStatus::Capacity,{}};
    ports_.frameEvents.publish(std::move(timers));units_.swap(next);
    if(resources && !ports_.monsters.commitLife(resources->take())) throw std::logic_error("Native aura resource commit lost its reservation");
    ports_.monsters.velocityModifier(id,units_.at(id).states.modifiers(actor.tick).velocityPercent);
    if(!writes && a.harmfulDurationPercent<100) ports_.monsters.shortenPoison(id,actor.tick,a.harmfulDurationPercent);
    if(!writes && healing && a.lifePerPulse>0) ports_.monsters.heal(id,a.lifePerPulse.raw());
    if(committedAffected) *committedAffected=affected;
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::consumeAuraMana(const ActorContext &actor,const AuraDefinition &a,bool affected) {
    if(a.manaPerPulse<=0) return {DomainStatus::Applied,std::monostate{}};
    const auto *player=ports_.players.find(actor.player);
    if(!player || !player->rules.character) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &recovery=next.players[actor.actor];
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(player->area));
    const auto observe=[&]{resources.sample(recovery.states,true);};
    const int state=player->rules.character->noManaRegenState;
    if(affected) {
        recovery.states.setNativeState(state,true);observe();
        const int bloodState=player->rules.character->bloodManaState;
        if(bloodState>=0 && recovery.states.hasState(bloodState,actor.tick)) {
            auto record=resources.record();
            int32_t threshold{};
            if(record.hp>=a.manaPerPulse) {
                std::optional<int> bloodSkill;
                for(const auto &effect:recovery.states.entries()) if(effect.spec.state.id==bloodState && effect.activeAt(actor.tick)) {
                    bloodSkill=effect.spec.source.definition;break;
                }
                if(!bloodSkill || !player->rules.skills || !player->rules.skills->formulas)
                    throw std::runtime_error("Missing native Blood Mana skill source");
                const auto &recordRule=player->rules.skills->formulas->skills.at(*bloodSkill);
                threshold=nativeShiftLeft(recordRule.number("param5"),8);
            }
            const auto payment=nativeManaPayment(record.hp,record.mana,a.manaPerPulse,true,threshold);
            if(!payment.accepted) {recovery.states.removeState(bloodState,observe);record=resources.record();}
            resources.seedResources(payment.life,record.mana,record.stamina);
            if(payment.accepted && payment.removeBloodMana) recovery.states.removeState(bloodState,observe);
        } else {
            auto record=resources.record();
            // AuraConsume may fail after the flag is set. It does not borrow
            // mana or cancel the effects which have already been executed.
            if(record.mana>=a.manaPerPulse) {
                record.mana=Fixed8::fromRaw(nativeSubtract(record.mana.raw(),a.manaPerPulse.raw()));
                resources.seedResources(record.hp,record.mana,record.stamina);
            }
        }
    } else {recovery.states.setNativeState(state,false);observe();}
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,resources.record()};
    edit.resourceBase=resources.maximum();edit.petLimitChanges=resources.petLimits();edit.transient=projectEffectAttributes(recovery.states,actor.tick);
    const auto previous=state_.players.find(actor.actor);
    if(previous!=state_.players.end()) for(const auto state:previous->second.states.stateIds(actor.tick))
        if(!recovery.states.hasState(state,actor.tick))
            edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,state,false});
    const auto before=previous==state_.players.end()?std::set<int>{}:previous->second.states.stateIds(actor.tick);
    for(const auto state:recovery.states.stateIds(actor.tick)) if(!before.contains(state))
        edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,state,true});
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) publishPlayers(next.players);
    return result;
}
StepStatus System::advanceAuraCycle(const ActorContext &actor,const PlayerState &p,const AuraSkillSpec &spec,PaladinCycle &cycle,uint64_t tick) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto &area=ports_.areas.at(p.area).definition;
    if(!cycle.pending) {
        cycle.callbackAffected=false;cycle.manaCommitted=false;cycle.pulseCommitted=false;
        cycle.progress={};cycle.damage.reset();cycle.recipientsPrepared=false;
        auto context=skills::formulaInput(p);
        cycle.aura=evaluateNativeAura(spec,cycle.rank,*p.rules.skills->formulas,context);
        cycle.occurrence=++paladinOccurrence_;
        cycle.duration=skills::periodicDuration(p,cycle.skill,cycle.rank,tick);
        cycle.targets={p.actor};cycle.target=0;cycle.pending=true;
    }
    const uint64_t duration=cycle.duration;
    while(cycle.target<cycle.targets.size()) {
        const auto id=cycle.targets[cycle.target];bool affected=false;
        DomainResult<> result{DomainStatus::Applied,std::monostate{}};
        if(id==p.actor) result=advanceAuraTarget(actor,p.actor,cycle.aura,id,duration,(uint64_t(1)<<61)|cycle.occurrence,0,cycle.progress,true,
            spec.serverDoFunction==65?&affected:nullptr);
        else if(spec.serverDoFunction==82) {
            const auto redeemed=redeemUnitCorpse(p.actor,id,cycle.skill,cycle.rank,tick);
            result=redeemed?DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{redeemed.status,{}};
            affected=redeemed && *redeemed.value;
        } else result=advanceAuraTarget(actor,p.actor,cycle.aura,id,duration,(uint64_t(1)<<61)|cycle.occurrence,
            cycle.damage.value_or(0),cycle.progress,false,&affected);
        if(isDeferred(result.status)) return StepStatus::Blocked;
        if(result) cycle.callbackAffected|=affected;
        if(id==p.actor && !cycle.recipientsPrepared) {
            if((spec.serverDoFunction==66 || spec.serverDoFunction==81) && area.town) {
                cycle.damage=0;cycle.recipientsPrepared=true;cycle.manaCommitted=true;
            }
            if(!cycle.damage) {
                auto context=skills::formulaInput(p);
                const auto after=evaluateNativeAura(spec,cycle.rank,*p.rules.skills->formulas,context);
                cycle.aura.minimumDamage=after.minimumDamage;cycle.aura.maximumDamage=after.maximumDamage;cycle.aura.radius=after.radius;
                const auto rolled=rollAuraDamage(p.actor,cycle.aura,tick);if(!rolled) return StepStatus::Blocked;
                cycle.damage=*rolled.value;
            }
            if(!cycle.recipientsPrepared) {
                auto recipients=auraCandidateRecipients(p.actor,cycle.aura,p.area,tick);
                cycle.targets.insert(cycle.targets.end(),recipients.begin(),recipients.end());cycle.recipientsPrepared=true;
            }
        }
        ++cycle.target;cycle.progress={};
    }
    cycle.pulseCommitted=true;
    if(!cycle.manaCommitted) {
        if(!consumeAuraMana(actor,cycle.aura,cycle.callbackAffected)) return StepStatus::Blocked;
        cycle.manaCommitted=true;
    }
    return StepStatus::Complete;
}
} // namespace d2x::server::effects

#include "system.hpp"
#include "pd2/equipped_skills.hpp"
#include "gameplay/skills/mana_payment.hpp"
#include "attribute_projection.hpp"
#include "gameplay/skills/targeting.hpp"
#include "gameplay/combat/geometry.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/skills/evaluation.hpp"
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
bool shortenAuraHarmfulDurations(CombatEffectSet &states,std::optional<PoisonStatus> &poison,uint64_t tick,int percentage,bool intrinsicPoison) {
    bool affected=false;
    if(poison && (intrinsicPoison || states.hasState(poison->damage.state,tick))) {
        const auto found=std::find_if(states.entries().begin(),states.entries().end(),
            [&](const auto &effect){return effect.spec.state.id==poison->damage.state;});
        if(intrinsicPoison || (found!=states.entries().end() && found->expiresAt)) {
            const auto previous=found!=states.entries().end() && found->expiresAt?found->nativeExpireFrame.value_or(nativeNarrow(uint32_t(*found->expiresAt))):
                poison->nativeExpireFrame.value_or(nativeNarrow(uint32_t(poison->until)));
            const auto until=nativeAuraDurationTick(tick,uint32_t(previous),percentage);
            if(found!=states.entries().end()) states.setExpiresAt(found->handle,until,tick);
            poison->until=until;poison->nativeExpireFrame=nativeNarrow(uint32_t(until));
            poison->removalDue=nativeEventDeadline(tick,*poison->nativeExpireFrame).due;affected=true;
        }
    }
    return states.shortenCurableCurses(tick,percentage) || affected;
}
std::vector<EntityId> System::auraCandidateRecipients(EntityId actor,const AuraDefinition &a,RegionId region,uint64_t tick,const monsters::Actor *source) const {
    return nativeAuraCandidateRecipients({actor,region,int32_t(a.radius),a.filter,a.serverDoFunction==65 || a.serverDoFunction==82,{},NativeAuraTargetMode::OwnerRange,tick},source);
}
std::vector<EntityId> System::nativeAuraCandidateRecipients(NativeAuraTargetQuery query,const monsters::Actor *source) const {
    if(query.mode==NativeAuraTargetMode::OwnerRange && query.radius<=0) return {};
    const auto actor=query.owner;const auto region=query.area;
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto owner=source?combat::ParticipantView{nullptr,source}:participants.find(actor);const auto &area=ports_.areas.at(region).definition;
    const uint32_t filter=query.filter || query.mode==NativeAuraTargetMode::TargetRange?query.filter:0x583;
    // Missiles currently have no ISVALIDTARGET flag; filter 0x400 excludes them.
    if((filter&(0x10|0x20)) || ((filter&8) && !(filter&0x400)))
        throw std::runtime_error("Aura recipient filter requires its native unit finder");
    if((filter&0x80000) && !query.tick) throw std::invalid_argument("Missing native JUSTHIT query frame");
    const auto sourcePosition=query.sourcePosition?*query.sourcePosition-area.origin:owner.position();
    const auto origin=query.center.value_or(sourcePosition+area.origin);
    const AreaDefinition *searchArea=&area;
    const auto *originRoom=area.activation.room(sourcePosition);
    if(query.mode==NativeAuraTargetMode::TargetRange) {
        if(!query.center || !int(origin.x) || !int(origin.y)) return {};
        const auto selected=ports_.areas.roomAtPosition(region,sourcePosition,int32_t(origin.x),int32_t(origin.y));
        if(!selected) return {};
        searchArea=&ports_.areas.at(selected->area).definition;
        originRoom=searchArea->activation.room(origin-searchArea->origin);
    }
    if(!originRoom) return {};
    const auto originIndex=size_t(originRoom-searchArea->activation.rooms().data());
    const bool checkNoAura=query.checkNoAura;
    const bool corpses=(filter&0x1000)!=0;
    if(corpses && !query.tick) throw std::invalid_argument("Missing native corpse query frame");
    std::vector<EntityId> result;
    for(const auto &link:searchArea->roomNear.at(originIndex)) {
        const auto *targetArea=ports_.areas.find(link.area);if(!targetArea) continue;
        const auto &definition=targetArea->definition;
        if(((filter&0x2000) || (filter&0x100)) && definition.town) continue;
        const int roomX=link.x*5-int(definition.origin.x),roomY=link.y*5-int(definition.origin.y);
        const auto room=std::find_if(definition.activation.rooms().begin(),definition.activation.rooms().end(),
            [&](const auto &bounds){return bounds.x==roomX && bounds.y==roomY;});
        if(room==definition.activation.rooms().end()) continue;
        const auto eligible=[&](combat::ParticipantView target) {
            if((query.mode==NativeAuraTargetMode::OwnerRange && target.id()==actor) || definition.activation.room(target.position())!=&*room ||
               !withinNativeAuraArea(origin,target.position()+definition.origin,query.radius)) return false;
            if(target.player) {
                if(!target.player->entered || target.player->area!=link.area || (corpses?target.player->persistent.player.hp>0:target.player->persistent.player.hp<=0)) return false;
            } else if(!target.monster || target.monster->area!=link.area || (corpses?!ports_.monsters.deadMode(target.id(),*query.tick):!target.monster->alive())) return false;
            if((filter&0x80) && !target.damageable()) return false;
            if((filter&0x400) && !target.damageable()) return false;
            if(filter&0x20000) {
                int range=owner.monster?owner.monster->rule.meleeRange:0;
                if(owner.player && owner.player->totals.equipment.weaponCount)
                    range=owner.player->totals.equipment.weapons[0].rangeAdder;
                if(!unitMeleeReach(area.collision,sourcePosition,owner.size(),
                    target.position()+definition.origin-area.origin,target.size(),range)) return false;
            }
            if((filter&0x80000) && !missileHitAllowed(target.id(),*query.tick)) return false;
            if((filter&0x10000) && !participants.auraAligned(owner,target)) return false;
            if((filter&0x8000) && !participants.canHarm(owner,target)) return false;
            if((filter&0x200) && !area.collision.missileSegment(
                {float(int(sourcePosition.x)),float(int(sourcePosition.y))},
                {float(int(target.position().x+definition.origin.x-area.origin.x)),float(int(target.position().y+definition.origin.y-area.origin.y))},{4,1})) return false;
            return true;
        };
        const auto roomIndex=size_t(room-definition.activation.rooms().begin());
        if(targetArea->rooms.empty() || !targetArea->rooms.at(roomIndex).loaded) continue;
        for(const auto id:ports_.areas.roomUnitOrder(link)) {
            const auto peer=participants.find(id);
            if(peer.player) {if((filter&1) && eligible(peer)) result.push_back(id);}
            else if(peer.monster && (filter&2)) {
                const auto &unit=*peer.monster;
                if(checkNoAura && unit.rule.noAura) continue;
                if(((filter&4) && !unit.rule.undead) || ((filter&0x4000) && unit.rule.boss) || ((filter&0x40000) && unit.rule.primeEvil)) continue;
                if(eligible(peer)) result.push_back(id);
            }
        }
    }

    return result;
}
void System::prepareImmediateAuraUnit(AuraUnitPublication &plan,const ActorContext &caster,const AuraDefinition &a,
    const SkillAuraChange &change,EntityId id,bool owner,const monsters::Actor *candidate) {
    if(plan.actors && plan.actors->removals.contains(id)) return;
    const auto active=plan.activeSources.find(id);
    auto *borrowed=active==plan.activeSources.end()?nullptr:active->second;
    if(borrowed) candidate=&borrowed->actor();
    if(!candidate && plan.actors) {
        const auto staged=plan.actors->actors.find(id);
        if(staged!=plan.actors->actors.end()) candidate=&staged->second;
    }
    const auto *live=candidate?candidate:ports_.monsters.find(id);
    const bool alive=live && (borrowed?(borrowed->read().life.raw()>0 || live->modeAlive):live->alive());
    if(!alive || (!owner && live->rule.noAura) || !live->rule.stateTarget.acceptsCurseList()) return;
    if(!live->rule.stats || !live->rule.formulas || !live->nativeStats || !live->nativeStats->sources || !a.targetWrites || a.serverDoFunction!=65)
        throw std::runtime_error("Missing native immediate aura monster sources");
    auto [entry,inserted]=plan.units.try_emplace(id);
    auto &unit=entry->second;
    if(inserted) {
        if(const auto current=units_.find(id);current!=units_.end()) {
            unit=current->second;plan.expectedEffects.emplace(id,current->second.states);
        } else {unit.area=live->area;unit.converted=bool(live->conversion);plan.expectedAbsent.insert(id);}
    }
    auto shadow=*live;
    const auto earlier=plan.life.find(id);
    const bool alreadyChanged=earlier!=plan.life.end() && earlier->second.changed;
    if(earlier!=plan.life.end()) {
        shadow.life=earlier->second.life.raw();shadow.maximumLife=earlier->second.maximum.raw();shadow.nativeStats=earlier->second.intrinsic;
        if(earlier->second.cureColdPoison) {shadow.poison.reset();shadow.chilledUntil=shadow.frozenUntil=0;shadow.shatterActive=false;}
        if(earlier->second.poison) shadow.poison=*earlier->second.poison;
        if(earlier->second.shatter) shadow.shatterActive=*earlier->second.shatter;
        if(earlier->second.curseControl) shadow.curseControl=*earlier->second.curseControl;
        if(earlier->second.stopMovement) {shadow.route.clear();shadow.moving=false;shadow.running=false;}
        if(earlier->second.hitDelay) shadow.nextHitTick=std::max(shadow.nextHitTick,*earlier->second.hitDelay);
        if(earlier->second.attraction) {shadow.attractionTarget=earlier->second.attraction->target;shadow.attractionUntil=earlier->second.attraction->until;}
        if(earlier->second.aiResetTimers) {shadow.aiResetTimers=*earlier->second.aiResetTimers;shadow.nativeAiResetTracked=true;}
    }
    std::optional<MonsterResourceTrace> local;
    if(!borrowed) local.emplace(shadow,caster.tick,&unit.states);
    auto &resources=borrowed?*borrowed:*local;
    resources.inheritStateCallbacks(unit.states);
    const auto &rules=*shadow.rule.stats;
    const auto sources=[&] {
        auto result=combatEffectStatSources(unit.states.entries(),caster.tick,true);
        return result;
    };
    CombatEffectSpec spec;spec.state=owner?a.ownerState:a.state;spec.source={CombatEffectSource::Skill,caster.actor,a.skill,a.rank};
    const auto duration=skillPeriodicDuration(nativeNarrow(uint32_t(caster.tick)),change.period);
    spec.duration=uint32_t(duration);spec.nativeState=true;spec.refreshPassivesOnRemoval=true;spec.modifiers=owner?a.ownerModifiers:a.modifiers;
    if(owner) {spec.nativeRemoval=NativeStateRemoval::BasicAura;spec.clearStateOnRemoval=live->rule.noManaRegenState;}
    std::vector<StatValue> writes;
    const auto &instructions=owner?a.ownerWrites:a.targetWrites;
    if(!instructions) throw std::runtime_error("Missing native unit aura writes");
    const auto callbacks=owner?a.ownerCallbackWrites:instructions->size();
    for(size_t index=0;index<callbacks;++index) {
        auto write=(*instructions)[index];
        const auto &name=rules.definitions.at(write.key.id).name;
        if(write.value<0 && !shadow.hireling && (name=="damageresist" || name=="magicresist" || name=="fireresist" ||
            name=="lightresist" || name=="coldresist" || name=="poisonresist") &&
            baseStatValue(rules,*resources.read().intrinsic->sources,write.key)>=100) write.value/=5;
        if(!write.value) continue;
        if(name=="velocitypercent" || name=="attackrate") write.value=std::max(write.value,shadow.rule.coldEffect);
        writes.push_back(write);
        if(name=="attackrate") writes.emplace_back(StatKey{rules.names.at("other_animrate"),0},write.value);
    }
    const auto adjustedCallbacks=writes.size();
    writes.insert(writes.end(),instructions->begin()+callbacks,instructions->end());
    const bool wasActive=unit.states.hasState(spec.state.id,caster.tick);
    std::optional<std::map<StatKey,int32_t>> oldValues;
    for(const auto &effect:unit.states.entries()) if(effect.spec.state.id==spec.state.id) {
        oldValues=prepareModifierStatSource(rules,{{},effect.spec.modifiers,combatEffectNativeValues(effect.spec)},StatOwnerKind::Monster)->base;break;
    }
    const auto procedure=[&](StatValue write)->std::optional<bool> {
        const auto &rule=rules.definitions.at(write.key.id);
        if(rule.name=="item_poisonlengthresist") {
            auto poison=resources.read().poison?*resources.read().poison:shadow.poison;
            const bool affected=shortenAuraHarmfulDurations(unit.states,poison,caster.tick,write.value,true);
            if(affected) resources.seedPoison(std::move(poison));
            return affected;
        }
        if(!rule.direct) return {};
        const auto currentSources=sources();const auto values=resources.values(currentSources);
        const auto current=rule.name=="hitpoints"?resources.read().life.raw():values.get(write.key);
        const auto maximum=rule.maximumStat?std::optional<int32_t>{values.get({*rule.maximumStat,0})}:std::nullopt;
        const auto next=nativeAuraDirectStatValue(current,write.value,maximum);
        if(next==current) return false;
        if(rule.name=="hitpoints") resources.seedLife(Fixed8::fromRaw(next));
        else resources.writeBaseStat(rule.name,next,currentSources);
        return true;
    };
    const auto applied=applyNativeAuraState(unit.states,spec,writes,adjustedCallbacks,rules,caster.tick,
        [&]{resources.sample(unit.states,true);},procedure);
    if(!applied.accepted) {if(inserted) {plan.units.erase(id);plan.expectedEffects.erase(id);plan.expectedAbsent.erase(id);}return;}
    const auto passiveState=shadow.rule.formulas->skills.at(a.skill).passiveState;
    if(passiveState>0) {
        std::vector<int> passives;
        for(const auto &source:resources.read().intrinsic->sources->contributions) if(source && source->source.kind==StatSourceKind::Passive &&
            shadow.rule.formulas->skills.at(source->source.definition).passiveState==passiveState) passives.push_back(source->source.definition);
        for(const int skill:passives) resources.withdrawIntrinsicSource(StatSourceKind::Passive,skill,sources());
    }
    resources.sample(unit.states);
    auto life=borrowed?resources.read():resources.take();life.changed|=alreadyChanged;
    if(earlier!=plan.life.end()) {
        life.cureColdPoison=earlier->second.cureColdPoison;
        if(!life.poison) life.poison=earlier->second.poison;
        if(!life.shatter) life.shatter=earlier->second.shatter;
        retainPreparedUnitControl(life,earlier->second);
    }
    if(const auto baseline=plan.expectedEffects.find(id);baseline!=plan.expectedEffects.end()) prepareUnitControl(shadow,baseline->second,unit.states,life,caster.tick);
    else prepareUnitControl(shadow,CombatEffectSet{},unit.states,life,caster.tick);
    plan.life.insert_or_assign(id,std::move(life));
    const auto *installed=unit.states.find(applied.handle);
    if(!installed) throw std::logic_error("Missing prepared immediate monster aura state");
    const auto values=prepareModifierStatSource(rules,{{},installed->spec.modifiers,combatEffectNativeValues(installed->spec)},StatOwnerKind::Monster);
    std::vector<StatValue> stats;for(const auto &[key,value]:values->base) stats.emplace_back(key,value);
    for(const auto &removed:applied.removed) if(!unit.states.hasState(removed.effect.spec.state.id,caster.tick)) {
        plan.facts.emplace_back(StateFact{id,1,live->area,removed.effect.spec.state.id,false});unit.nativeStats.erase(removed.effect.spec.state.id);
    }
    if(!wasActive || !oldValues || *oldValues!=values->base) plan.facts.emplace_back(StateFact{id,1,live->area,spec.state.id,true,stats});
    unit.nativeStats[spec.state.id]=std::move(stats);plan.velocity[id]=unit.states.modifiers(caster.tick).velocityPercent;
}
void System::prepareUnitItemAuras(AuraUnitPublication &plan,monsters::Actor &candidate,uint64_t tick,const AuraRecipientPreparation &recipient) {
    auto changes=std::exchange(candidate.preparedItemAuras,{});
    for(const auto &change:changes) prepareUnitItemAura(plan,candidate,change,tick,recipient);
}
void System::prepareUnitItemAura(AuraUnitPublication &plan,monsters::Actor &candidate,const SkillAuraChange &change,uint64_t tick,const AuraRecipientPreparation &prepareRecipient) {
    if(!candidate.rule.stats || !candidate.rule.formulas || !candidate.rule.auraPrograms || !candidate.nativeStats)
        throw std::runtime_error("Missing prepared unit item-aura inputs");
    const ActorContext caster{candidate.owner.value_or(PlayerId{}),candidate.id,candidate.area,ports_.areas.at(candidate.area).generation,0,tick};
    auto &timers=candidate.itemAuraTimers;
    const auto current=[&] {
        auto shadow=candidate;
        if(const auto active=plan.activeSources.find(candidate.id);active!=plan.activeSources.end()) {
            const auto &life=active->second->read();shadow.life=life.life.raw();shadow.maximumLife=life.maximum.raw();shadow.nativeStats=life.intrinsic;
            if(life.poison) shadow.poison=*life.poison;
            if(life.shatter) shadow.shatterActive=*life.shatter;
            if(life.curseControl) shadow.curseControl=*life.curseControl;
            return shadow;
        }
        if(const auto life=plan.life.find(candidate.id);life!=plan.life.end()) {
            shadow.life=life->second.life.raw();shadow.maximumLife=life->second.maximum.raw();shadow.nativeStats=life->second.intrinsic;
            if(life->second.poison) shadow.poison=*life->second.poison;
            if(life->second.shatter) shadow.shatterActive=*life->second.shatter;
            if(life->second.curseControl) shadow.curseControl=*life->second.curseControl;
        }
        return shadow;
    };
        ItemAuraCandidateHooks hooks;
        hooks.reserveEventOrder=[&]{return reserveAuraEventOrder();};
        hooks.executeImmediate=[&](const SkillAuraChange &activation) {
            if(activation.equippedSkill) {
                const auto programs=ports_.monsters.skillPrograms();
                if(!programs || !candidate.rule.states) throw std::runtime_error("Missing immediate PD2 unit equipped skill program");
                auto source=current();
                if(!plan.units.contains(candidate.id)) {
                    UnitEffect state;state.area=source.area;
                    if(const auto live=units_.find(candidate.id);live!=units_.end()) state=live->second;
                    prepareUnitEffects(plan,candidate.id,state,monsters::LifeChange{source.id,source.revision,
                        Fixed8::fromRaw(nativeChecked(source.life)),Fixed8::fromRaw(nativeChecked(source.maximumLife)),source.nativeStats,false},tick);
                }
                auto &state=plan.units.at(candidate.id);
                const auto active=plan.activeSources.find(candidate.id);
                auto *borrowed=active==plan.activeSources.end()?nullptr:active->second;
                std::optional<MonsterResourceTrace> local;if(!borrowed) local.emplace(source,tick,&state.states);
                auto &resources=borrowed?*borrowed:*local;
                applyPd2EquippedBuff(state.states,caster,activation,*programs,*candidate.rule.states,
                    [&](attributes::AuraSourceObservation){resources.sample(state.states,true);},plan.facts,true);
                auto life=borrowed?resources.read():resources.take();life.changed=true;
                plan.life.insert_or_assign(candidate.id,std::move(life));
                if(const auto *installed=state.states.findState(activation.state)) state.nativeStats[activation.state]=nativeEffectStats(*programs,installed->spec);
                plan.velocity[candidate.id]=state.states.modifiers(tick).velocityPercent;
                return;
            }
            const auto &record=candidate.rule.formulas->skills.at(activation.skill);
            if(!activation.formula || !candidate.rule.auraPrograms->contains(activation.skill))
                throw std::runtime_error("Missing immediate unit aura program");
            const auto input=activation.formula->input(*candidate.rule.stats);
            SkillFormulaValues values(*candidate.rule.formulas,input);
            const auto delay=values.calculation(FormulaDomain::Skill,activation.skill,activation.rank,"delay");
            if(record.number("srvdofunc")!=65 || !record.number("intown") || record.number("usemanaondo") ||
                record.number("decquant") || !record.reference("srvmissile").empty() || !delay || *delay)
                throw std::runtime_error("Unit item aura requires its complete forced do program");
            const auto aura=evaluateNativeAura(candidate.rule.auraPrograms->at(activation.skill),activation.rank,*candidate.rule.formulas,input);
            auto source=current();
            prepareImmediateAuraUnit(plan,caster,aura,activation,candidate.id,true,&source);
            source=current();
            for(const auto recipient:auraCandidateRecipients(candidate.id,aura,candidate.area,caster.tick,&source)) {
                if(!prepareRecipient) throw std::runtime_error("Missing shared unit aura recipient preparation");
                prepareRecipient(caster,aura,activation,recipient);
            }
        };
    hooks.removeState=[&](const SkillAuraChange &withdrawal) {
        auto source=current();
        if(!plan.units.contains(candidate.id)) {
            UnitEffect unit;unit.area=source.area;
            if(const auto live=units_.find(candidate.id);live!=units_.end()) unit=live->second;
            prepareUnitEffects(plan,candidate.id,unit,monsters::LifeChange{source.id,source.revision,
                Fixed8::fromRaw(nativeChecked(source.life)),Fixed8::fromRaw(nativeChecked(source.maximumLife)),source.nativeStats,false},tick);
        }
        auto &unit=plan.units.at(candidate.id);auto &states=unit.states;
        const bool active=states.hasState(withdrawal.state,tick);
        const auto activeSource=plan.activeSources.find(candidate.id);
        auto *borrowed=activeSource==plan.activeSources.end()?nullptr:activeSource->second;
        std::optional<MonsterResourceTrace> local;
        if(!borrowed) local.emplace(source,tick,&states);
        auto &resources=borrowed?*borrowed:*local;
        resources.inheritStateCallbacks(states);
        states.setNativeState(withdrawal.state,false);resources.sample(states,true);
        const auto removed=states.removeState(withdrawal.state,[&]{resources.sample(states,true);});
        auto life=borrowed?resources.read():resources.take();life.changed=true;
        unit.nativeStats.erase(withdrawal.state);plan.life.insert_or_assign(candidate.id,std::move(life));
        plan.velocity[candidate.id]=states.modifiers(tick).velocityPercent;
        if(active || !removed.empty()) plan.facts.emplace_back(StateFact{candidate.id,1,candidate.area,withdrawal.state,false});
    };
    executeItemAuraCallback(timers,change,tick,hooks);
    if(!plan.units.contains(candidate.id)) {
        UnitEffect unit;unit.area=candidate.area;
        if(const auto live=units_.find(candidate.id);live!=units_.end()) unit=live->second;
        prepareUnitEffects(plan,candidate.id,unit,monsters::LifeChange{candidate.id,candidate.revision,
            Fixed8::fromRaw(candidate.life),Fixed8::fromRaw(candidate.maximumLife),candidate.nativeStats,true},tick);
    }
    auto &life=plan.life.at(candidate.id);life.itemAuraTimers=timers;life.changed=true;
}
} // namespace d2x::server::effects

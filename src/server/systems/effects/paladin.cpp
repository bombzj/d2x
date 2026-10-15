#include "system.hpp"
#include "attribute_projection.hpp"
#include "gameplay/skills/targeting.hpp"
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
#include "gameplay/combat/geometry.hpp"
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
    // MISSILES_Initialize/CreateMissileFromParams clear ISVALIDTARGET.
    // The shipped BladesOfIce filter includes missiles (8) together with
    // the valid-target requirement (0x400), so none reach its callback.
    // Other noncombat-unit filters still need their own native finder.
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
            if((filter&0x80000) && !missileHitAllowed(target.id(),*query.tick)) return false;
            if((filter&0x10000) && !participants.auraAligned(owner,target)) return false;
            if((filter&0x8000) && !participants.canHarm(owner,target)) return false;
            // SKILLS_AuraCallback's 0x20000 is UNITS_IsInMeleeRange,
            // also used by the native Zeal/Fury GUID successor finder.
            if(filter&0x20000) {
                const auto destination=target.position()+definition.origin-area.origin;
                const int range=owner.player?(owner.player->totals.equipment.weaponCount?
                    owner.player->totals.equipment.weapons[0].rangeAdder:0):owner.monster?owner.monster->rule.meleeRange:0;
                if(!owner.id() || !unitMeleeReach(area.collision,sourcePosition,owner.size(),destination,target.size(),range)) return false;
            }
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
void System::stageAuraActors(AuraUnitPublication &plan,std::map<EntityId,monsters::Actor> actors,size_t allocatedIds) {
    if(plan.actors) throw std::invalid_argument("Duplicate staged aura actors");
    plan.actors=std::make_shared<monsters::ActorPublication>(ports_.monsters.prepareActorPublication(std::move(actors),allocatedIds));
}
void System::stageSummonEvents(AuraUnitPublication &plan,EntityId id,int skill,int rank,const SkillFormulaCatalog &catalog,uint64_t) const {
    const auto definition=catalog.skills.find(skill);
    if(definition==catalog.skills.end() || definition->second.unitEvents.front().event<0) return;
    plan.skillEvents.push_back({id,definition->second.suppressionState,{CombatEffectSource::Skill,id,skill,rank},definition->second.unitEvents});
}
void System::prepareStagedSkillEvents(AuraUnitPublication &plan,uint64_t tick) const {
    for(const auto &registration:plan.skillEvents) {
        const auto id=registration.unit;UnitEffect unit;
        if(const auto prepared=plan.units.find(id);prepared!=plan.units.end()) unit=prepared->second;
        else if(const auto live=units_.find(id);live!=units_.end()) unit=live->second;
        const auto *actor=ports_.monsters.find(id);
        if(plan.actors) if(const auto staged=plan.actors->actors.find(id);staged!=plan.actors->actors.end()) actor=&staged->second;
        if(!actor) throw std::invalid_argument("Missing native summon event recipient");
        unit.area=actor->area;unit.states.registerUnitEvents(registration.state,registration.source,registration.bindings);
        if(plan.units.contains(id)) plan.units.at(id)=std::move(unit);
        else {
            prepareUnitEffects(plan,id,unit,{},tick);
            if(plan.life.contains(id)) {
                plan.velocity.insert_or_assign(id,unit.states.modifiers(tick).velocityPercent);
                if(std::find(plan.lifeOrder.begin(),plan.lifeOrder.end(),id)==plan.lifeOrder.end()) plan.lifeOrder.push_back(id);
            } else plan.eventsOnly.insert(id);
        }
    }
    plan.skillEvents.clear();
    prepareUnitTimers(plan);
}
void System::stageAuraSource(AuraUnitPublication &plan,EntityId id,AuraUnitPublication::SourcePreparation source) {
    if(!source || !plan.actors || !plan.actors->actors.contains(id) || !plan.sources.emplace(id,std::move(source)).second)
        throw std::invalid_argument("Invalid staged unit source preparation");
}
void System::prepareStagedItemAuras(AuraUnitPublication &plan,uint64_t tick,const AuraRecipientPreparation &recipient) {
    if(!plan.actors) return;
    for(auto &[id,candidate]:plan.actors->actors) {
        const auto source=plan.sources.find(id);
        if(source==plan.sources.end()) {
            prepareUnitItemAuras(plan,candidate,tick,recipient);
            // SetSummonPassiveStats assigns inherited aura skills to the
            // right slot at birth, in original sumskill order.
            if(candidate.druidPet && !ports_.monsters.find(id) && candidate.rule.auraPrograms)
                for(const auto skill:candidate.nativeSkillOrder)
                    if(candidate.rule.auraPrograms->contains(skill))
                        prepareUnitAuraSelection(plan,id,skill,tick,recipient);
            continue;
        }
        candidate.preparedItemAuras.clear();
        CombatEffectSet states;
        if(const auto prepared=plan.units.find(id);prepared!=plan.units.end()) states=prepared->second.states;
        else if(const auto live=units_.find(id);live!=units_.end()) states=live->second.states;
        source->second(candidate,states,[&](monsters::Actor &actor,MonsterResourceTrace &resources,const SkillAuraChange &change) {
            struct ActiveSourceScope {
                std::map<EntityId,MonsterResourceTrace *> &sources;EntityId actor;MonsterResourceTrace *previous{};
                ~ActiveSourceScope() {if(previous) sources.at(actor)=previous;else sources.erase(actor);}
            } scope{plan.activeSources,actor.id};
            if(const auto previous=plan.activeSources.find(actor.id);previous!=plan.activeSources.end()) scope.previous=previous->second;
            plan.activeSources[actor.id]=&resources;
            const auto sourceId=actor.id;
            if(const auto prepared=plan.units.find(sourceId);prepared!=plan.units.end()) {
                resources.inheritPassiveCache(prepared->second.states.passiveRefreshEpoch());resources.inheritStateCallbacks(prepared->second.states);
            } else if(const auto live=units_.find(sourceId);live!=units_.end()) {
                resources.inheritPassiveCache(live->second.states.passiveRefreshEpoch());resources.inheritStateCallbacks(live->second.states);
            } else resources.inheritStateCallbacks(CombatEffectSet{});
            resources.readStateSources([&,sourceId] {
                std::vector<ModifierStatSource> states;
                if(const auto prepared=plan.units.find(sourceId);prepared!=plan.units.end())
                    states=combatEffectStatSources(prepared->second.states.entries(),tick,true);
                else if(const auto live=units_.find(sourceId);live!=units_.end())
                    states=combatEffectStatSources(live->second.states.entries(),tick,true);
                return states;
            });
            auto life=resources.read();
            if(const auto previous=plan.life.find(actor.id);previous!=plan.life.end()) retainPreparedUnitControl(life,previous->second);
            plan.life.insert_or_assign(actor.id,std::move(life));
            prepareUnitItemAura(plan,actor,change,tick,recipient);
            actor.nativeStats=resources.read().intrinsic;actor.life=resources.read().life.raw();actor.maximumLife=resources.read().maximum.raw();
            if(resources.read().poison) actor.poison=*resources.read().poison;
            if(resources.read().shatter) actor.shatterActive=*resources.read().shatter;
            if(const auto life=plan.life.find(actor.id);life!=plan.life.end() && life->second.curseControl) actor.curseControl=*life->second.curseControl;
        });
        plan.sources.erase(source);
        if(const auto life=plan.life.find(id);life!=plan.life.end()) {
            life->second.expectedRevision=candidate.revision;life->second.life=Fixed8::fromRaw(nativeChecked(candidate.life));
            life->second.maximum=Fixed8::fromRaw(nativeChecked(candidate.maximumLife));life->second.intrinsic=candidate.nativeStats;
            life->second.itemAuraTimers=candidate.itemAuraTimers;life->second.changed=true;
        }
    }
    prepareStagedSkillEvents(plan,tick);
}
bool System::validateAuraUnits(const AuraUnitPublication &plan) const {
    if(plan.timers.size()!=plan.units.size()) return false;
    for(const auto &[id,timers]:plan.timers) if(!plan.units.contains(id) || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.resetTimers) if(!plan.life.contains(id) || !plan.life.at(id).aiResetTimers || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.skillTimers) if(timers.owner!=FrameEventOwner{FrameEventUnit::Monster,id} || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.nativeSkillTimers) if(timers.owner!=FrameEventOwner{FrameEventUnit::Monster,id} || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,timers]:plan.regenerationTimers) if(!plan.life.contains(id) || !ports_.frameEvents.validate(timers)) return false;
    for(const auto &[id,life]:plan.life) if(life.periodicEvents &&
        (life.periodicEvents->edit.owner!=FrameEventOwner{FrameEventUnit::Monster,id} || !validatePeriodicEvents(*life.periodicEvents))) return false;
    size_t registrations=0;
    for(const auto &[id,life]:plan.life) if(life.specialTargetNode.value_or(false)) {
        const auto *unit=ports_.monsters.find(id);
        if(unit && !unit->owner && !unit->specialTargetOrder) ++registrations;
    }
    if(!ports_.monsters.canRegisterSpecialTargets(registrations)) return false;
    if(!plan.activeSources.empty() || !plan.sources.empty() || !plan.skillEvents.empty()) return false;
    if(plan.actors && !ports_.monsters.validateActors(*plan.actors)) return false;
    for(const auto &[id,unit]:plan.units) {
        const auto guard=plan.recipients.find(id);const auto *recipientArea=ports_.areas.find(unit.area);
        if(guard==plan.recipients.end() || !recipientArea || recipientArea->generation!=guard->second.areaGeneration) return false;
        const auto *actor=ports_.monsters.find(id);const auto life=plan.life.find(id);
        const monsters::Actor *staged=nullptr;
        if(plan.actors) {
            const auto candidate=plan.actors->actors.find(id);
            if(candidate!=plan.actors->actors.end()) staged=&candidate->second;
        }
        if(plan.eventsOnly.contains(id) && life==plan.life.end()) {
            if(!actor && !staged) return false;
            if((staged?staged->area:actor->area)!=unit.area) return false;
        } else if(staged) {
            if(staged->area!=unit.area || life==plan.life.end() || life->second.expectedRevision!=staged->revision ||
               life->second.maximum.raw()<0 || life->second.life.raw()<0 || life->second.life>life->second.maximum || !life->second.intrinsic) return false;
            const uint64_t increments=uint64_t(life->second.changed)+uint64_t(plan.velocity.at(id)!=staged->effectVelocity);
            if(staged->revision>UINT64_MAX-increments) return false;
            if((life->second.curseControl || life->second.reselectAi) && staged->aiFunctionRevision==UINT64_MAX) return false;
            if((life->second.tauntPostActions || life->second.restoreNeutral || life->second.aiThinkAt) && staged->aiThinkRevision==UINT64_MAX) return false;
            const bool alignmentChanged=staged->rule.stats && life->second.intrinsic->values.get({staged->rule.stats->names.at("alignment"),0})!=int(staged->alignment());
            if((alignmentChanged || life->second.curseControl || life->second.attraction || life->second.detachedMinion) && staged->allegianceRevision==UINT64_MAX) return false;
        } else if(actor) {
            if(actor->area!=unit.area || life==plan.life.end() || !ports_.monsters.canCommitLife(life->second)) return false;
            const uint64_t increments=uint64_t(life->second.changed)+uint64_t(plan.velocity.at(id)!=actor->effectVelocity);
            if(actor->revision>UINT64_MAX-increments) return false;
        } else return false;
        const auto current=units_.find(id);
        if(plan.expectedAbsent.contains(id)) {if(current!=units_.end()) return false;}
        else if(current==units_.end() || !current->second.states.sharesSnapshot(plan.expectedEffects.at(id))) return false;
    }
    return true;
}
void System::prepareUnitEffects(AuraUnitPublication &plan,EntityId id,const UnitEffect &unit,std::optional<monsters::LifeChange> life,uint64_t tick) const {
    plan.tick=tick;
    if((life && life->actor!=id) || plan.units.contains(id)) throw std::invalid_argument("Duplicate prepared aura unit");
    const monsters::Actor *actor=ports_.monsters.find(id);
    if(plan.actors) if(const auto staged=plan.actors->actors.find(id);staged!=plan.actors->actors.end()) actor=&staged->second;
    if(life && !actor) throw std::invalid_argument("Missing prepared control unit");
    if(const auto current=units_.find(id);current!=units_.end()) {
        plan.expectedEffects.emplace(id,current->second.states);
        if(life) prepareUnitControl(*actor,current->second.states,unit.states,*life,tick);
    } else {
        plan.expectedAbsent.insert(id);
        if(life) prepareUnitControl(*actor,CombatEffectSet{},unit.states,*life,tick);
    }
    plan.units.emplace(id,unit);
    if(life) {plan.lifeOrder.push_back(id);plan.life.emplace(id,std::move(*life));plan.velocity.emplace(id,unit.states.modifiers(tick).velocityPercent);}
    prepareUnitTimers(plan);
}
void System::publishAuraUnits(AuraUnitPublication &plan) noexcept {
    if(plan.timers.size()!=plan.units.size()) std::terminate();
    for(auto &[id,timers]:plan.timers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.resetTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.skillTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.nativeSkillTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    for(auto &[id,timers]:plan.regenerationTimers) {(void)id;ports_.frameEvents.publish(std::move(timers));}
    const auto publishLife=[&](EntityId id,monsters::LifeChange &life) {
        auto *staged=plan.actors && plan.actors->actors.contains(id)?&plan.actors->actors.at(id):nullptr;
        if(staged) {
            if(life.changed) {
                const auto alignment=staged->alignment();
                staged->life=life.life.raw();staged->maximumLife=life.maximum.raw();staged->nativeStats=std::move(life.intrinsic);++staged->revision;
                if(life.cureColdPoison) {staged->poison.reset();staged->chilledUntil=staged->frozenUntil=0;staged->shatterActive=false;}
                if(life.poison) staged->poison=*life.poison;
                if(life.wound) staged->wound=*life.wound;
                if(life.chilledUntil) staged->chilledUntil=*life.chilledUntil;
                if(life.frozenUntil) staged->frozenUntil=*life.frozenUntil;
                if(life.stunnedUntil) staged->stunnedUntil=*life.stunnedUntil;
                if(life.restoreNeutral) {
                    staged->route.clear();staged->moving=staged->running=false;staged->busyUntil=staged->reactionUntil=0;staged->neutralAt.reset();
                }
                if(life.shatter) staged->shatterActive=*life.shatter;
                if(life.itemAuraTimers) staged->itemAuraTimers=std::move(*life.itemAuraTimers);
                if(life.periodicEvents) publishPeriodicEvents(*life.periodicEvents);
                if(life.curseControl) {
                    if(life.reselectAi || (staged->curseControl.ai!=life.curseControl->ai && staged->curseControl.ai!=CurseAi::Confuse && life.curseControl->ai!=CurseAi::Confuse && staged->curseControl.ai!=CurseAi::Attract && life.curseControl->ai!=CurseAi::Attract)) {
                        ++staged->aiFunctionRevision;staged->aiThinkAt=0;
                    }
                    staged->curseControl=*life.curseControl;
                }
                if(life.tauntPostActions) staged->mainOwnerCleared=true;
                if(life.restoreNeutral || life.tauntPostActions || life.aiThinkAt) {++staged->aiThinkRevision;staged->aiThinkAt=life.aiThinkAt.value_or(0);}
                if(life.tauntTarget) staged->movementTarget=staged->tauntTarget=*life.tauntTarget;
                if(life.detachedMinion) {
                    staged->detachedMinion=*life.detachedMinion;
                    if(*life.detachedMinion) staged->nativeMinionOwner=EntityId{};
                }
                if(life.nativeMinions) staged->nativeMinions=std::move(*life.nativeMinions);
                if(life.specialTargetNode && !*life.specialTargetNode) staged->specialTargetOrder=0;
                if(life.stopMovement) {staged->route.clear();staged->moving=false;staged->running=false;}
                if(life.hitDelay) staged->nextHitTick=std::max(staged->nextHitTick,*life.hitDelay);
                if(life.attraction) {staged->attractionTarget=life.attraction->target;staged->attractionUntil=life.attraction->until;}
                if(life.aiResetTimers) {staged->aiResetTimers=std::move(*life.aiResetTimers);staged->nativeAiResetTracked=true;}
                if(alignment!=staged->alignment() || life.curseControl || life.attraction || life.detachedMinion) ++staged->allegianceRevision;
            }
            if(staged->effectVelocity!=plan.velocity.at(id)) {staged->effectVelocity=plan.velocity.at(id);++staged->revision;}
        } else if(!ports_.monsters.commitLife(std::move(life))) std::terminate();
    };
    for(const auto id:plan.lifeOrder) publishLife(id,plan.life.at(id));
    for(auto &[id,life]:plan.life) if(std::find(plan.lifeOrder.begin(),plan.lifeOrder.end(),id)==plan.lifeOrder.end()) publishLife(id,life);
    if(plan.actors) {
        for(const auto &[id,candidate]:plan.actors->actors) plan.velocity.erase(id);
        ports_.monsters.publishActors(*plan.actors);
    }
    while(!plan.units.empty()) {
        auto node=plan.units.extract(plan.units.begin());const auto current=units_.find(node.key());
        if(const auto *actor=ports_.monsters.find(node.key());actor && !actor->alive()) {
            const FrameEventOwner owner{FrameEventUnit::Monster,node.key()};
            ports_.frameEvents.cancel(owner,FrameEventType::PeriodicSkills);
            ports_.frameEvents.cancel(owner,FrameEventType::PeriodicStats);
            ports_.frameEvents.cancel(owner,FrameEventType::StatRegeneration);
        }
        if(current==units_.end()) units_.insert(std::move(node));else current->second=std::move(node.mapped());
    }
    for(const auto &[id,velocity]:plan.velocity) ports_.monsters.velocityModifier(id,velocity);
}
std::vector<StatValue> nativeEffectStats(const SkillRules &rules,const CharacterModifiers &m) {
    CombatEffectSpec effect;effect.modifiers=m;return nativeEffectStats(rules,effect);
}
std::vector<StatValue> nativeEffectStats(const StatRuleCatalog &rules,const CombatEffectSpec &effect) {
    const auto list=prepareModifierStatSource(rules,{{},effect.modifiers,combatEffectNativeValues(effect)},StatOwnerKind::Other);
    std::vector<StatValue> result;for(const auto &[key,value]:list->base) result.emplace_back(key,value);
    return result;
}
std::vector<StatValue> nativeEffectStats(const SkillRules &rules,const CombatEffectSpec &effect) {
    if(!rules.stats) throw std::runtime_error("Missing native effect stat schema");
    return nativeEffectStats(*rules.stats,effect);
}
AuraDefinition System::evaluateUnitAura(EntityId id,int skill,int rank,uint64_t tick) const {
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !unit->rule.auraPrograms || !unit->rule.auraPrograms->contains(skill) || !unit->rule.formulas)
        throw std::runtime_error("Missing native unit aura program");
    auto sampled=unitStatValues(id,tick);if(!sampled) throw std::runtime_error("Missing native unit aura formula sources");
    auto context=skills::formulaInput(*unit,std::move(*sampled));
    return evaluateNativeAura(unit->rule.auraPrograms->at(skill),rank,*unit->rule.formulas,context);
}
DomainResult<> System::queueAuraPulse(EntityId source,const AuraDefinition &a,const std::vector<EntityId> &targets,uint64_t occurrence,int64_t damage) {
    const combat::Participants participants{ports_.players,ports_.monsters,ports_.social};
    std::map<RegionId,std::vector<EntityId>> rooms;
    for(const auto id:targets) {
        const auto target=participants.find(id);
        if(!target.player && !target.monster) continue;
        const auto region=target.player?target.player->area:target.monster->area;
        if(target.aliveIn(region)) rooms[region].push_back(id);
    }
    std::vector<combat::SpellImpact> impacts;
    for(auto &[region,recipients]:rooms) {
        combat::SpellImpact pulse{source,source,region,DamageType(a.element),damage,std::move(recipients)};
        pulse.occurrence=occurrence;pulse.hitClass=uint8_t(a.hitClass);pulse.reaction=true;pulse.unblockable=true;
        pulse.knockback=(a.resultFlags&8)!=0;pulse.nativeAura=true;
        impacts.push_back(std::move(pulse));
    }
    auto plan=ports_.combat.prepareSpells(std::move(impacts));if(!plan) return {plan.status,{}};
    ports_.combat.commitSpells(std::move(*plan.value));return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<int32_t> System::rollAuraDamage(EntityId id,const AuraDefinition &a,uint64_t tick) {
    if(a.element<0 || a.maximumDamage<=0) return {DomainStatus::Applied,0};
    const auto *player=ports_.players.findActor(id);const auto *monster=ports_.monsters.find(id);
    if(!player && !monster) return {DomainStatus::InvalidActor,{}};
    const auto expected=player?player->combatRandom:monster->combatRandom;auto random=expected;
    const auto spread=nativeSubtract(a.maximumDamage.raw(),a.minimumDamage.raw());
    const auto damage=nativeAdd(a.minimumDamage.raw(),int32_t(limitedRandom(random,uint32_t(spread))));
    DomainResult<> result;
    if(player) result=ports_.transactions.consumeRandom({player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick},expected,random);
    else result=ports_.monsters.consumeRandom(id,expected,random);
    return {result.status,result?std::optional<int32_t>{damage}:std::nullopt};
}
DomainResult<> System::holyFreezeShatter(EntityId id,uint64_t duration,uint64_t tick) {
    const auto *player=ports_.players.findActor(id);
    if(!player) return ports_.monsters.holyFreeze(id,tick+duration,tick);
    auto states=player->transient.effects?*player->transient.effects:CombatEffectSet{};
    if(!player->transient.effects && !player->transient.states.empty()) return {DomainStatus::Unavailable,{}};
    auto random=player->combatRandom;const bool active=rollRandom(random)%100<20;
    const auto &definition=player->rules.skills->shatter;
    const bool changed=states.hasState(definition.id,tick)!=active;states.setNativeState(definition.id,active);
    const ActorContext actor{player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick};
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    edit.random=transactions::UnitRandomEdit{player->combatRandom,random};edit.transient=projectEffectAttributes(states,tick);
    if(changed && !definition.noSend) edit.publicFacts.emplace_back(StateFact{id,0,player->area,definition.id,active});
    auto plan=ports_.transactions.prepare(std::move(edit));return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
}
DomainResult<> System::advanceAuraTarget(const ActorContext &actor,EntityId source,const AuraDefinition &a,EntityId id,uint64_t duration,
    uint64_t occurrence,int32_t damage,AuraTargetProgress &progress,bool owner,bool *affected) {
    if(!progress.state) {
        const auto result=paladinAura(actor,a,id,duration,owner,false,ports_.players.findActor(source)?EntityId{}:source,affected);
        if(result.status==DomainStatus::Capacity) return result;
        progress.accepted=bool(result);progress.state=true;
    }
    if(!owner && progress.accepted) {
        if(a.element>=0 && a.maximumDamage>0) {
            if(!progress.pulse) {
                const auto result=queueAuraPulse(source,a,std::vector<EntityId>{id},occurrence,damage);
                if(!result) return result;progress.pulse=true;
            }
            if(ports_.combat.advanceAuraPulse(source,occurrence,{actor.tick})==StepStatus::Blocked) return {DomainStatus::Capacity,{}};
        }
        if(a.serverDoFunction==81 && !progress.post) {
            const auto result=holyFreezeShatter(id,duration,actor.tick);
            if(result.status==DomainStatus::Capacity) return result;
            progress.post=true;
        }
    }
    if(progress.accepted && !owner && (a.serverDoFunction==66 || a.serverDoFunction==81) && affected) *affected=true;
    return {DomainStatus::Applied,std::monostate{}};
}
EffectApplication applyNativeAuraState(CombatEffectSet &states,const CombatEffectSpec &spec,
    const std::optional<std::vector<StatValue>> &writes,size_t callbackWrites,const StatRuleCatalog &rules,uint64_t tick,
    const std::function<void()> &observe,const std::function<std::optional<bool>(StatValue)> &procedure,bool *affected) {
    if(!writes) return states.apply(spec,tick,observe);
    if(callbackWrites>writes->size()) throw std::invalid_argument("Invalid native aura callback boundary");
    auto empty=spec;empty.modifiers={};empty.nativeValues=std::map<StatKey,int32_t>{};
    empty.stacking=EffectStacking::SkillLevel;
    auto applied=states.apply(std::move(empty),tick,observe);
    if(!applied.accepted) return applied;
    for(size_t index=0;index<callbackWrites;++index) {
        const auto &write=(*writes)[index];
        if(procedure) if(const auto handled=procedure(write)) {
            if(affected) *affected|=*handled;
            continue;
        }
        if(!states.writeNativeStats(applied.handle,std::span{&write,size_t(1)},rules,spec,observe))
            throw std::logic_error("Missing allocated native aura state");
        if(affected) *affected=true;
    }
    std::vector<StatValue> ordered;
    ordered.emplace_back(StatKey{rules.names.at("modifierlist_skill"),0},spec.source.definition);
    ordered.emplace_back(StatKey{rules.names.at("modifierlist_level"),0},spec.source.level);
    ordered.insert(ordered.end(),writes->begin()+callbackWrites,writes->end());
    if(!states.writeNativeStats(applied.handle,ordered,rules,spec,observe))
        throw std::logic_error("Missing allocated native aura state");
    return applied;
}
static void applyImmediateAuraRecipient(CombatEffectSet &states,const ActorContext &caster,const ActorContext &actor,const AuraDefinition &a,
    const SkillAuraChange &change,const StatRuleCatalog &rules,int noManaState,int passiveState,bool owner,
    const attributes::AuraSourceObserver &observe,const attributes::AuraSourceReader &read,std::vector<DomainFact> &facts,PlayerPoisonCandidate &poison) {
    if(!a.ownerWrites || !a.targetWrites || a.serverDoFunction!=65) throw std::runtime_error("Missing immediate BasicAura program");
    const auto duration=skillPeriodicDuration(nativeNarrow(uint32_t(actor.tick)),change.period);
    CombatEffectSpec spec;spec.state=owner?a.ownerState:a.state;spec.source={CombatEffectSource::Skill,caster.actor,a.skill,a.rank};
    spec.duration=uint32_t(duration);spec.nativeState=true;spec.refreshPassivesOnRemoval=true;
    spec.clearStateOnRemoval=owner?noManaState:-1;spec.modifiers=owner?a.ownerModifiers:a.modifiers;
    spec.nativeRemoval=owner?NativeStateRemoval::BasicAura:NativeStateRemoval::Default;
    const bool wasActive=states.hasState(spec.state.id,actor.tick);
    std::optional<std::map<StatKey,int32_t>> previousValues;
    for(const auto &effect:states.entries()) if(effect.spec.state.id==spec.state.id) {
        previousValues=prepareModifierStatSource(rules,{{},effect.spec.modifiers,combatEffectNativeValues(effect.spec)},StatOwnerKind::Player)->base;
        break;
    }
    auto writes=owner?a.ownerWrites:a.targetWrites;const auto rawCallbacks=owner?a.ownerCallbackWrites:writes->size();
    std::vector<StatValue> adjusted;
    for(size_t index=0;index<rawCallbacks;++index) {
        auto write=(*writes)[index];const auto &name=rules.definitions.at(write.key.id).name;
        if(!write.value) continue;
        if(name=="velocitypercent" || name=="attackrate") write.value=std::max(write.value,-50);
        adjusted.push_back(write);
        if(name=="attackrate") adjusted.emplace_back(StatKey{rules.names.at("other_animrate"),0},write.value);
    }
    const auto callbacks=adjusted.size();
    adjusted.insert(adjusted.end(),writes->begin()+rawCallbacks,writes->end());writes=std::move(adjusted);
    const auto publish=[&](std::optional<ResourceValues> resources={},std::vector<int> withdrawn={}) {
        auto projection=projectEffectAttributes(states,actor.tick);
        observe({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),resources,std::move(withdrawn)});
    };
    const auto procedure=[&](StatValue write)->std::optional<bool> {
        const auto &rule=rules.definitions.at(write.key.id);
        if(rule.name=="item_poisonlengthresist") {
            const bool affected=shortenAuraHarmfulDurations(states,poison.poison,actor.tick,write.value);
            poison.changed|=affected;return affected;
        }
        if(!rule.direct) return {};
        const auto frame=read();
        ResourceValues resources{Fixed8::fromRaw(frame.currentResources.at({rules.names.at("hitpoints"),0})),
            Fixed8::fromRaw(frame.currentResources.at({rules.names.at("mana"),0})),
            Fixed8::fromRaw(frame.currentResources.at({rules.names.at("stamina"),0}))};
        auto *value=rule.name=="hitpoints"?&resources.life:rule.name=="mana"?&resources.mana:rule.name=="stamina"?&resources.stamina:nullptr;
        if(!value) throw std::runtime_error("Immediate aura direct stat requires a unit base writer");
        const auto maximum=rule.maximumStat?std::optional<int32_t>{frame.stats.get({*rule.maximumStat,0})}:std::nullopt;
        const auto next=nativeAuraDirectStatValue(value->raw(),write.value,maximum);
        if(next==value->raw()) return false;
        *value=Fixed8::fromRaw(next);publish(resources);return true;
    };
    const auto application=applyNativeAuraState(states,spec,writes,callbacks,rules,actor.tick,[&]{publish();},procedure);
    if(!application.accepted) return;
    if(passiveState>0) publish({},std::vector<int>{passiveState});
    for(const auto &removed:application.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
        facts.emplace_back(StateFact{actor.actor,0,actor.area,removed.effect.spec.state.id,false});
    const auto *installed=states.find(application.handle);
    if(!installed) throw std::logic_error("Missing immediate aura owner list");
    const auto values=prepareModifierStatSource(rules,{{},installed->spec.modifiers,combatEffectNativeValues(installed->spec)},StatOwnerKind::Player);
    std::vector<StatValue> stats;for(const auto &[key,value]:values->base) stats.emplace_back(key,value);
    if(!wasActive || !previousValues || *previousValues!=values->base)
        facts.emplace_back(StateFact{actor.actor,0,actor.area,spec.state.id,true,std::move(stats)});
}
void applyImmediateAuraOwner(CombatEffectSet &states,const ActorContext &actor,const AuraDefinition &a,const SkillAuraChange &change,
    const StatRuleCatalog &rules,int noManaState,int passiveState,const attributes::AuraSourceObserver &observe,
    const attributes::AuraSourceReader &read,std::vector<DomainFact> &facts,PlayerPoisonCandidate &poison) {
    applyImmediateAuraRecipient(states,actor,actor,a,change,rules,noManaState,passiveState,true,observe,read,facts,poison);
}
void applyImmediateAuraTarget(CombatEffectSet &states,const ActorContext &caster,const ActorContext &recipient,const AuraDefinition &a,const SkillAuraChange &change,
    const StatRuleCatalog &rules,int passiveState,const attributes::AuraSourceObserver &observe,
    const attributes::AuraSourceReader &read,std::vector<DomainFact> &facts,PlayerPoisonCandidate &poison) {
    applyImmediateAuraRecipient(states,caster,recipient,a,change,rules,-1,passiveState,false,observe,read,facts,poison);
}
namespace {
std::vector<RemovedCombatEffect> removeDefinition(CombatEffectSet &states,EntityId source,int skill,const std::function<void()> &observe) {
    std::vector<EffectHandle> handles;
    std::vector<RemovedCombatEffect> removed;
    for(const auto &e:states.entries()) if(e.spec.source.entity==source && e.spec.source.definition==skill) handles.push_back(e.handle);
    for(const auto h:handles) {
        auto values=states.remove(h,observe);removed.insert(removed.end(),values.begin(),values.end());
    }
    return removed;
}
}
DomainResult<> System::removeAura(const ActorContext &actor,int skill) {
    const auto *p=ports_.players.find(actor.player);if(!p) return {DomainStatus::InvalidActor,{}};
    auto next=state_;auto &r=next.players[actor.actor];PlayerResourceTrace resources(*p,actor.tick,{},ports_.areas.statTime(p->area));
    const auto removed=removeDefinition(r.states,actor.actor,skill,[&]{resources.sample(r.states,true);});
    transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,resources.record()};
    edit.resourceBase=resources.maximum();
    for(const auto &effect:removed) if(!r.states.hasState(effect.effect.spec.state.id,actor.tick))
        edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,effect.effect.spec.state.id,false});
    edit.transient=projectEffectAttributes(r.states,actor.tick);
    auto plan=ports_.transactions.prepare(std::move(edit));
    if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) publishPlayers(next.players);return result;
}
CharacterModifiers System::stateModifiers(EntityId id,int state,uint64_t tick) const {
    const auto *effects=unitEffects(id);
    if(effects) for(const auto &e:effects->entries()) if(e.spec.state.id==state && e.activeAt(tick)) return e.spec.modifiers;
    return {};
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
                    if(base>=100) write.value/=5;
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
                if(edit.player.mana<a.manaPerPulse) return {DomainStatus::Unavailable,{}};
                edit.player.mana-=a.manaPerPulse;
                resources.seedResources(edit.player.hp,edit.player.mana,edit.player.stamina);
                CombatEffectSpec stop;stop.state.id=state;stop.source=spec.source;stop.duration=duration;r.states.apply(std::move(stop),actor.tick,observe);
                edit.player=resources.record();
            }
        }
        edit.resourceBase=resources.maximum();
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
            if(record.hp<a.manaPerPulse) {
                recovery.states.removeState(bloodState,observe);
                record=resources.record();record.hp=Fixed8::fromRaw(256);
                resources.seedResources(record.hp,record.mana,record.stamina);
            } else {
                std::optional<int> bloodSkill;
                for(const auto &effect:recovery.states.entries()) if(effect.spec.state.id==bloodState && effect.activeAt(actor.tick)) {
                    bloodSkill=effect.spec.source.definition;break;
                }
                if(!bloodSkill || !player->rules.skills || !player->rules.skills->formulas)
                    throw std::runtime_error("Missing native Blood Mana skill source");
                const auto &recordRule=player->rules.skills->formulas->skills.at(*bloodSkill);
                const auto threshold=nativeShiftLeft(recordRule.number("param5"),8);
                record.hp=Fixed8::fromRaw(nativeSubtract(record.hp.raw(),a.manaPerPulse.raw()));
                resources.seedResources(record.hp,record.mana,record.stamina);
                if(record.hp.raw()<threshold) recovery.states.removeState(bloodState,observe);
            }
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
    edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(recovery.states,actor.tick);
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
    const auto key=actor.player;
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
        if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
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

}

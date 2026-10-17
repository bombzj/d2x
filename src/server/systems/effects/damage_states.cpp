#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/effects/native_status.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>

namespace d2x::server::effects {
AuraUnitPublication System::prepareDamageStates(monsters::Actor candidate,UnitDamageStates damage,uint64_t tick) const {
    AuraUnitPublication plan;
    stageDamageStates(plan,std::move(candidate),std::move(damage),tick);return plan;
}
void System::stageDamageStates(AuraUnitPublication &plan,monsters::Actor candidate,UnitDamageStates damage,uint64_t tick) const {
    prepareMonsterModifierEvents(candidate,ports_.monsters.find(candidate.id));
    const auto id=candidate.id;
    if(!candidate.alive() && ((candidate.killMinionsOnDeath && !candidate.minionDeathHandled) ||
        (candidate.ownsNativeMinions && !candidate.ownershipDeathHandled))) {
        auto expanded=ports_.monsters.prepareMinionDeaths(std::move(candidate),tick,plan.actors?&plan.actors->actors:nullptr);
        for(auto &unit:expanded) {
            const auto other=unit.id;const auto area=unit.area;const auto position=unit.position;
            if(other==id || !unit.alive()) {
                stageDamageStates(plan,std::move(unit),other==id?damage:UnitDamageStates{},tick);
                if(const auto *previous=ports_.monsters.find(other);other!=id && previous && previous->alive())
                    plan.facts.emplace_back(HitFact{other,1,area,0,true,position,0,0});
            } else if(plan.actors && plan.actors->actors.contains(other)) plan.actors->actors.at(other)=std::move(unit);
            else {
                std::map<EntityId,monsters::Actor> actors;actors.emplace(other,std::move(unit));
                auto additional=ports_.monsters.prepareActorPublication(std::move(actors),0);
                plan.actors->actors.merge(additional.actors);plan.actors->expected.merge(additional.expected);
            }
        }
        return;
    }
    UnitEffect unit;
    if(const auto prepared=plan.units.find(id);prepared!=plan.units.end()) {
        unit=prepared->second;plan.units.erase(prepared);plan.life.erase(id);plan.velocity.erase(id);
        plan.timers.erase(id);plan.resetTimers.erase(id);plan.skillTimers.erase(id);plan.regenerationTimers.erase(id);
        plan.eventsOnly.erase(id);std::erase(plan.lifeOrder,id);
    } else if(const auto found=units_.find(id);found!=units_.end()) unit=found->second;
    std::map<EntityId,monsters::Actor> actors;actors.emplace(id,std::move(candidate));
    if(plan.actors && plan.actors->actors.contains(id)) plan.actors->actors.at(id)=std::move(actors.begin()->second);
    else {
        auto publication=ports_.monsters.prepareActorPublication(std::move(actors),0);
        if(!plan.actors) plan.actors=std::make_shared<monsters::ActorPublication>(std::move(publication));
        else {plan.actors->actors.merge(publication.actors);plan.actors->expected.merge(publication.expected);}
    }
    const auto &actor=plan.actors->actors.at(id);
    if(!actor.rule.stats || !actor.rule.states || !actor.nativeStats) throw std::invalid_argument("Missing native damage state inputs");
    const auto &rules=*actor.rule.stats;
    unit.area=actor.area;const auto previousStates=unit.states.stateIds(tick);
    MonsterResourceTrace resources(actor,tick,&unit.states);
    const auto observe=[&]{resources.sample(unit.states,true);};
    for(const auto state:damage.clearStateFlags) {
        unit.states.setNativeState(state,false);observe();
    }
    if(const auto *previous=ports_.monsters.find(id);previous && previous->interruption!=actor.interruption)
        unit.states.endModeEffects({},observe);
    if(damage.deathDelay) unit.states.setNativeState(actor.rule.deathDelayState,true);
    else if(!actor.alive()) unit.states.setNativeState(actor.rule.deathDelayState,false);
    if(damage.status) {
        auto spec=*damage.status;spec.nativeDirectAllocation=true;spec.nativeState=true;spec.nativeValues.emplace();
        const auto installed=unit.states.apply(spec,tick,observe);
        if(!installed.accepted) throw std::logic_error("Native direct status allocation refused");
        if(!unit.states.writeNativeStats(installed.handle,damage.statusWrites,rules,spec,observe))
            throw std::logic_error("Native direct status list disappeared");
    }
    if(damage.curePoison && !damage.potion) unit.states.removeState(actor.rule.hitStates.poison.id,observe);
    if(damage.cureCold && !damage.potion) {
        unit.states.removeState(actor.rule.coldState,observe);unit.states.removeState(actor.rule.frozenState.id,observe);
        unit.states.setNativeState(actor.rule.shatterState.id,false);
    }
    const auto install=[&](CombatStateDefinition state,EntityId owner,uint64_t until,std::span<const StatValue> writes,NativeStatusPolicy policy={}) {
        if(state.id<0 || !until || until<tick) return;
        const auto application=applyNativeStatus(unit.states,state,{CombatEffectSource::NativeState,owner,0,0},until,tick,rules,writes,policy,observe);
        if(!application.accepted) throw std::logic_error("Native damage status rejected its direct list");
    };
    if(damage.chilledUntil) {
        const std::array<StatValue,3> writes{{{rules.names.at("velocitypercent"),actor.rule.coldEffect},
            {rules.names.at("attackrate"),actor.rule.coldEffect},{rules.names.at("other_animrate"),actor.rule.coldEffect}}};
        install(actor.rule.states->at(actor.rule.coldState),damage.source,damage.chilledUntil,writes,{NativeStateRemoval::Cold,actor.rule.shatterState.id});
    }
    if(damage.frozenUntil) install(actor.rule.frozenState,damage.source,damage.frozenUntil,{}, {NativeStateRemoval::Freeze});
    if(damage.stunnedUntil) install(actor.rule.states->at(actor.rule.stunState),damage.source,damage.stunnedUntil,{});
    else if(damage.clearStun && unit.states.hasState(actor.rule.stunState,tick)) unit.states.removeState(actor.rule.stunState,observe);
    if(damage.poisonApplied && actor.poison) {
        const auto &poison=*actor.poison;
        const std::array<StatValue,1> writes{{{rules.names.at("hpregen"),nativeSubtract(0,nativeChecked(poison.damage.rate))}}};
        install(actor.rule.states->at(poison.damage.state),poison.damage.actualSource?poison.damage.actualSource:damage.source?damage.source:poison.source,poison.until,writes);
    }
    if(damage.burn>0 && damage.burnFrames && !unit.states.hasState(actor.rule.fireShrineState,tick)) {
        const auto &state=actor.rule.burningState;const auto key=StatKey{rules.names.at("hpregen"),0};
        int32_t existing=0;
        for(const auto &entry:unit.states.entries()) if(entry.spec.state.id==state.id && entry.spec.nativeValues) {
            const auto rate=entry.spec.nativeValues->find(key);
            if(rate!=entry.spec.nativeValues->end()) existing=nativeSubtract(0,rate->second);
        }
        if(damage.burn>=existing) {
            const std::array<StatValue,1> writes{{{key,nativeSubtract(0,nativeChecked(damage.burn))}}};
            install(state,damage.source,tick+damage.burnFrames,writes);
        }
    }
    if(damage.woundApplied && actor.wound) {
        const std::array<StatValue,1> writes{{{rules.names.at("hpregen"),nativeSubtract(0,nativeChecked(actor.wound->rate))}}};
        if(actor.wound->until>=tick) applyNativeStatus(unit.states,actor.rule.states->at(actor.rule.openWoundsState),
            {CombatEffectSource::NativeState,actor.wound->source,0,1},actor.wound->until,tick,rules,writes,{},observe);
    }
    const bool newPotion=damage.potion && !unit.states.hasState(damage.potion->state.id,tick);
    if(damage.potion) applyPotionState(unit.states,*damage.potion,id,tick,rules,*actor.rule.states,damage.potionRestoration,observe);
    if(actor.alive() && (damage.resetRegeneration || newPotion ||
        (damage.burn>0 && damage.burnFrames && !unit.states.hasState(actor.rule.fireShrineState,tick)))) plan.resetRegeneration.insert(id);
    if(!actor.alive()) {
        const auto kind=actor.rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
        unit.states.onDeath(kind,observe);unit.states.clearNonDeathStates(kind,*actor.rule.states,tick,observe);
    }
    resources.sample(unit.states);
    auto life=resources.take();
    if(actor.alive() && damage.freezeThinkAt) {life.aiThinkAt=damage.freezeThinkAt;life.changed=true;}
    if(actor.alive() && damage.contact && life.intrinsic && nativeStatValue(life.intrinsic->values,rules,"hpregen")) plan.resetRegeneration.insert(id);
    if(!actor.alive() && !actor.itemAuraTimers.empty()) {life.itemAuraTimers=std::vector<ItemAuraTimer>{};life.changed=true;}
    const auto before=unitStates(id,tick);auto after=unit.states.stateIds(tick);
    after.insert(actor.corpse.states.begin(),actor.corpse.states.end());
    for(const int state:previousStates) if(!after.contains(state) && !actor.corpse.states.contains(state)) {
        plan.facts.emplace_back(StateFact{id,1,actor.area,state,false});unit.nativeStats.erase(state);
    }
    for(const auto &entry:unit.states.entries()) if(entry.spec.nativeState && after.contains(entry.spec.state.id)) {
        const auto values=nativeEffectStats(rules,entry.spec);
        if(!before.contains(entry.spec.state.id) || !unit.nativeStats.contains(entry.spec.state.id) || unit.nativeStats.at(entry.spec.state.id)!=values)
            plan.facts.emplace_back(StateFact{id,1,actor.area,entry.spec.state.id,true,values});
        unit.nativeStats[entry.spec.state.id]=values;
    }
    prepareUnitEffects(plan,id,unit,std::move(life),tick);
}
}

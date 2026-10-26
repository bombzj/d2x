#include "map_values.hpp"
#include "../monster_resources.hpp"
#include "../monster_sources.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/effects/queries.hpp"
#include <stdexcept>

namespace d2x::server::attributes {
namespace {
void prepareEvent(monsters::Actor &actor,int velocity,uint64_t tick,int lifePercent,int lifeFactor) {
    if(!actor.nativeStats || !actor.rule.stats || !actor.rule.stats->pd2 || !actor.rule.states ||
       !actor.rule.states->contains(213)) throw std::runtime_error("Missing PD2 scene birth state");
    if(actor.initialEffects && actor.initialEffects->findState(213)) return;
    const auto &rules=*actor.rule.stats;
    auto states=actor.initialEffects?std::make_shared<CombatEffectSet>(*actor.initialEffects):std::make_shared<CombatEffectSet>();
    MonsterResourceTrace resources(actor,tick,states.get());
    // 41d49 38BFC0: unsigned maximum, clamp INT_MAX, divide before multiply.
    // Only MAXHP is written; do not invent a second full-heal operation.
    if(lifePercent) {
        const auto maximum=std::min(uint32_t(resources.read().maximum.raw()),uint32_t(INT32_MAX));
        resources.writeBaseStat("maxhp",nativeMultiply(int32_t(maximum/100),lifePercent),{});
    } else if(lifeFactor) resources.writeBaseStat("maxhp",nativeMultiply(resources.read().maximum.raw(),lifeFactor),{});
    CombatEffectSpec spec;spec.state=actor.rule.states->at(213);spec.nativeState=spec.nativeDirectAllocation=true;
    spec.source={CombatEffectSource::Environment,actor.id,180,0};spec.nativeValues.emplace();
    for(const auto id:{25,329,331,330,332}) {
        if(!rules.definitions.contains(id)) throw std::runtime_error("Missing PD2 scene birth property");
        writeNativeBaseStat(*spec.nativeValues,rules,{uint16_t(id),0},id==25?velocity:nativeMultiply(velocity,2));
    }
    spec.modifiers=nativeCharacterModifiers(StatSnapshot{*spec.nativeValues},rules);
    if(!states->apply(spec,tick).accepted) throw std::runtime_error("PD2 scene birth state admission failed");
    resources.sample(*states,true);
    auto next=resources.take();actor.nativeStats=std::move(next.intrinsic);
    actor.life=next.life.raw();actor.maximumLife=next.maximum.raw();
    actor.effectVelocity=states->modifiers(tick).velocityPercent;actor.initialEffects=std::move(states);
}
}
void preparePd2SceneMonster(monsters::Actor &actor,int lifePercent,uint64_t tick) {prepareEvent(actor,5,tick,lifePercent,0);}
void preparePd2EventMonster(monsters::Actor &actor,int velocity,uint64_t tick,int lifeFactor) {prepareEvent(actor,velocity,tick,0,lifeFactor);}
void preparePd2HiddenSceneMonster(monsters::Actor &actor,uint64_t tick) {
    if(!actor.nativeStats || !actor.rule.stats || !actor.rule.states || !actor.rule.states->contains(146))
        throw std::runtime_error("Missing native Lucion birth states");
    auto states=actor.initialEffects?std::make_shared<CombatEffectSet>(*actor.initialEffects):std::make_shared<CombatEffectSet>();
    MonsterResourceTrace resources(actor,tick,states.get());const auto sources=combatEffectStatSources(states->entries(),tick,true);
    for(const auto name:{"fireresist","lightresist","coldresist","poisonresist","magicresist","damageresist"})
        resources.writeBaseStat(name,200,sources);
    CombatEffectSpec spec;spec.state=actor.rule.states->at(146);spec.nativeState=spec.nativeDirectAllocation=true;
    spec.source={CombatEffectSource::Environment,actor.id,146,0};spec.nativeValues.emplace();
    writeNativeBaseStat(*spec.nativeValues,*actor.rule.stats,{89,0},-100);
    spec.modifiers=nativeCharacterModifiers(StatSnapshot{*spec.nativeValues},*actor.rule.stats);
    if(!states->apply(spec,tick).accepted) throw std::runtime_error("Native Lucion birth state rejected");
    states->setNativeState(8,true);resources.sample(*states,true);
    auto life=resources.take();actor.nativeStats=std::move(life.intrinsic);actor.life=life.life.raw();actor.maximumLife=life.maximum.raw();
    actor.initialEffects=std::move(states);actor.targetFlagsDisabled=actor.pd2NoPathDisabled=true;
}
void preparePd2MapMonster(monsters::Actor &actor,const pd2::MapInstance &map,uint64_t tick,bool dead,std::span<const StatValue> runtimeGlobal) {
    if(!actor.nativeStats || !actor.rule.stats || !actor.rule.stats->pd2) return;
    preparePd2BossScaling(actor,map,tick,dead);
    if(actor.initialEffects && actor.initialEffects->findState(map.catalog->state.id)) return;
    const auto &rules=*actor.rule.stats;
    MonsterResourceTrace resources(actor,tick);
    const auto before=resources.values({});
    const auto prepared=actor.rule.alignment?pd2::MapUnitProperties{}:
        pd2::prepareMapUnitProperties(map.catalog->properties,map.properties,false,
            nativeStatValue(before,rules,"mindamage"),nativeStatValue(before,rules,"maxdamage"),resources.read().maximum.raw());
    if(prepared.maximumLife) resources.writeBaseStat("maxhp",*prepared.maximumLife,{});
    CombatEffectSpec spec;spec.state=map.catalog->state;spec.nativeState=spec.nativeDirectAllocation=true;
    spec.source={CombatEffectSource::Environment,actor.id,map.destination.level,0};spec.nativeValues.emplace();
    for(const auto &value:prepared.values) writeNativeBaseStat(*spec.nativeValues,rules,value.key,value.value);
    StatSnapshot bases;
    for(const auto id:map.catalog->properties.sunderResistances)
        bases.values.emplace(StatKey{id,0},baseStatValue(rules,*resources.read().intrinsic->sources,{id,0}));
    auto properties=map.properties;
    properties.global.insert(properties.global.end(),runtimeGlobal.begin(),runtimeGlobal.end());
    for(const auto &value:pd2::prepareMapSunderProperties(map.catalog->properties,properties,int(actor.area),bases))
        writeNativeBaseStat(*spec.nativeValues,rules,value.key,value.value);
    spec.modifiers=nativeCharacterModifiers(StatSnapshot{*spec.nativeValues},rules);
    auto states=actor.initialEffects?std::make_shared<CombatEffectSet>(*actor.initialEffects):std::make_shared<CombatEffectSet>();
    if(!states->apply(spec,tick).accepted) throw std::runtime_error("PD2 monster map state admission failed");
    resources.observeSourceWrites([&](StatValue value,EntityId owner,const StatSnapshot &values,std::shared_ptr<const StatList> root) {
        dispatchMonsterItemAura(actor,resources,value,owner,values,std::move(root),{});
    });
    const auto sample=[&]{resources.sample(*states,true);};
    sample();
    if(dead) {
        const auto kind=actor.rule.boss?EffectUnitKind::Boss:EffectUnitKind::Monster;
        states->onDeath(kind,sample);states->clearNonDeathStates(kind,*actor.rule.states,tick,sample);
    }
    if(!dead && !actor.owner) resources.seedLife(resources.read().maximum);
    auto next=resources.take();actor.nativeStats=std::move(next.intrinsic);
    actor.life=next.life.raw();actor.maximumLife=next.maximum.raw();
    actor.effectVelocity=states->modifiers(tick).velocityPercent;
    actor.initialEffects=std::move(states);
}
}

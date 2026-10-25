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
void preparePd2MapMonster(monsters::Actor &actor,const pd2::MapInstance &map,uint64_t tick,bool dead) {
    if(!actor.nativeStats || !actor.rule.stats || !actor.rule.stats->pd2) return;
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
    for(const auto &value:pd2::prepareMapSunderProperties(map.catalog->properties,map.properties,int(actor.area),bases))
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

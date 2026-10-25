#include "map_state.hpp"
#include "../attribute_projection.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <stdexcept>

namespace d2x::server::effects {
std::optional<MapPlayerStateChange> preparePd2MapPlayerState(CombatEffectSet &states,const pd2::MapCatalog &catalog,const pd2::MapInstance *map,
    const StatRuleCatalog &rules,EntityId recipient,uint64_t tick,const attributes::AuraSourceObserver &observe) {
    const auto sample=[&] {
        auto value=projectEffectAttributes(states,tick);
        observe({std::move(*value.sources),std::move(value.states),std::move(value.effects),{}});
    };
    bool removed=false;
    if(const auto *current=states.findState(catalog.state.id)) {
        if(map && current->spec.source.kind==CombatEffectSource::Environment &&
            current->spec.source.entity==recipient && current->spec.source.definition==map->destination.level) return {};
        states.remove(current->handle,sample);
        removed=true;
    }
    if(!map) return removed?std::optional{MapPlayerStateChange{catalog.state.id,false,{}}}:std::nullopt;
    CombatEffectSpec spec;spec.state=catalog.state;spec.nativeState=true;spec.nativeDirectAllocation=true;
    spec.source={CombatEffectSource::Environment,recipient,map->destination.level,0};
    spec.nativeValues.emplace();
    const auto values=pd2::prepareMapUnitProperties(catalog.properties,map->properties,true,0,0,0);
    for(const auto &value:values.values) writeNativeBaseStat(*spec.nativeValues,rules,value.key,value.value);
    spec.modifiers=nativeCharacterModifiers(StatSnapshot{*spec.nativeValues},rules);
    // 0x388A40 builds this list before merging it into the unit. Publishing
    // an empty source and then each input separately would expose resource
    // maxima and callbacks that the native map posting never observes.
    const auto installed=states.apply(spec,tick,sample);
    if(!installed.accepted) throw std::runtime_error("PD2 map state admission failed");
    MapPlayerStateChange result{catalog.state.id,true,{}};
    for(const auto &[key,value]:*spec.nativeValues) result.values.emplace_back(key,value);
    return result;
}
}

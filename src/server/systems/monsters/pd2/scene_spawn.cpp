#include "../system.hpp"
#include "server/area_store.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "server/systems/attributes/pd2/map_values.hpp"

namespace d2x::server::monsters {
DomainResult<EntityId> System::admitPd2SceneMonster(RegionId region,Vec position,int definition,std::string key,uint64_t tick,int binding) {
    const auto *area=ports_.areas.find(region);
    if(!area || !area->definition.pd2Map || key.empty()) return {DomainStatus::InvalidRequest,{}};
    for(const auto &[id,actor]:state_.actors)
        if(actor.area==region && actor.identity.spawnKey==key) return {DomainStatus::Applied,id};
    const auto prototype=area->definition.nativeMonsterClasses.find(definition);
    if(prototype==area->definition.nativeMonsterClasses.end()) return {DomainStatus::NotImplemented,{}};
    if(!area->definition.activation.room(position)) return {DomainStatus::Unavailable,{}};
    if(state_.actors.size()>=65536 || ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    auto identity=prototype->second.identity;identity.spawnKey=std::move(key);
    identity.pd2Binding=binding;
    identity.ownerSpawnKey.clear();identity.origin=SpawnOrigin::Preset;
    const auto &rule=prototype->second.rule;
    if(rule.boss) identity.rank=MonsterRank::Boss;
    Admission request{std::move(identity),prototype->second.implementation,region,position,rule.alignment==0,rule,{},tick};
    // 38A060 performs the direct allocation after the scene's own clearance
    // search. Do not consume another native ring search or its room seed.
    auto random=ports_.random;ComponentPaletteState palettes;
    const auto id=EntityId{ports_.ids.cursor()};
    auto prepared=prepareAdmissionActor(request,id,random,palettes);
    if(!prepared) return {prepared.status,{}};
    auto actor=std::move(*prepared.value);
    if(area->definition.pd2Map->controller==45 && (definition==1112 || definition==1113))
        attributes::preparePd2HiddenSceneMonster(actor,tick);
    if(pd2ControllerDeath(actor)) actor.rewardComplete=false;
    const auto allocated=1+actor.birthEquipmentItems;
    auto publication=prepareActorPublication({{id,std::move(actor)}},allocated);
    if(!validateActors(publication)) return {DomainStatus::Stale,{}};
    publishComponents(palettes);publishActors(publication);ports_.random=random;
    return {DomainStatus::Applied,id};
}
EntityId System::pd2RathmaPartner(EntityId id) const {
    const auto *unit=find(id);
    if(!unit || unit->identity.pd2Binding<0 || unit->identity.pd2Binding>1 ||
       (unit->identity.nativeClass!=935 && unit->identity.nativeClass!=936)) return {};
    const auto *area=ports_.areas.find(unit->area);
    if(!area || !area->definition.pd2Map || area->definition.pd2Map->controller!=43) return {};
    for(const auto &[otherId,other]:state_.actors)
        if(otherId!=id && other.area==unit->area && other.identity.pd2Binding==(unit->identity.pd2Binding^1) &&
           other.identity.nativeClass==(unit->identity.nativeClass==935?936:935)) return otherId;
    return {};
}
bool System::pd2ControllerDeath(const Actor &actor) const {
    const auto *area=ports_.areas.find(actor.area);
    const auto map=area?area->definition.pd2Map:nullptr;
    if(!map) return false;
    const int type=actor.identity.nativeClass;
    return (map->controller==11 && type==922) || (map->controller==14 && type==921) ||
        (map->controller==41 && (type==704 || type==705 || type==709)) || (map->controller==42 && type==789) ||
        (map->controller==43 && type>=933 && type<=936) || (map->controller==44 && type>=989 && type<=991) ||
        (map->controller==45 && type==1112);
}
}

#include "../system.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"

namespace d2x::server::population {
DomainResult<EntityId> System::advancePd2PresetBirth(QuestBirth &birth,RegionId region,std::string_view name,Vec point,TickContext tick) {
    const auto *area=ports_.areas.find(region);
    if(!area || !area->definition.pd2Map || !area->definition.nativeQuestMonsters.contains(name)) return {DomainStatus::NotImplemented,{}};
    if(birth.name.empty()) {birth.area=region;birth.generation=area->generation;birth.name=name;birth.position=point;}
    if(birth.area!=region || birth.generation!=area->generation || birth.name!=name) return {DomainStatus::Stale,{}};
    if(birth.cursor.phase!=DensityRoom::Phase::Done) {
        if(ports_.monsters.modifiersPending()) return {DomainStatus::Conflict,{}};
        const auto *room=area->definition.activation.room(point);
        if(!room || !ports_.areas.loaded(region,point)) return {DomainStatus::Unavailable,{}};
        auto &population=state_.areas[region];
        if(population.generation!=area->generation) {population={};population.generation=area->generation;}
        const auto status=populateDensity(region,size_t(room-area->definition.activation.rooms().data()),tick,{},&birth);
        if(status!=StepStatus::Complete) return {status==StepStatus::Blocked?DomainStatus::Conflict:DomainStatus::NotImplemented,{}};
    }
    return {DomainStatus::Applied,birth.cursor.spawned?birth.cursor.leader:EntityId{}};
}
}

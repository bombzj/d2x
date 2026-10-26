#include "../system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "server/player_store.hpp"
#include "server/systems/travel/system.hpp"
#include <algorithm>

namespace d2x::server::world {
DomainResult<> System::appendPd2MapGlobal(const std::shared_ptr<const pd2::MapInstance> &map,StatValue program) {
    const auto &travel=ports_.travel.read();
    if(!map || travel.pd2Map!=map || !travel.pd2MapPortal || (pd2ProgramMap_ && pd2ProgramMap_!=map)) return {DomainStatus::Stale,{}};
    if(map->properties.global.size()+pd2RuntimeGlobal_.size()>=24) return {DomainStatus::Capacity,{}};
    pd2ProgramMap_=map;pd2RuntimeGlobal_.push_back(program);
    for(auto &[id,area]:ports_.areas.areas_) {
        (void)id;if(area.definition.pd2Map==map) area.definition.pd2RuntimeGlobal=pd2RuntimeGlobal_;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<uint64_t> System::requestMapArea(RegionId destination,const std::shared_ptr<const pd2::MapInstance> &map) {
    const auto &travel=ports_.travel.read();
    if(!map || travel.pd2Map!=map || !travel.pd2MapPortal) return {DomainStatus::Stale,{}};
    return request(destination,map);
}
bool System::cancelPd2Preparation(const std::shared_ptr<const pd2::MapInstance> &map) {
    if(!map) return false;
    std::vector<RegionId> regions;
    for(const auto &[id,area]:ports_.areas.all()) {
        if(area.definition.pd2Map!=map) continue;
        if(!area.provisionalPd2Map || std::any_of(area.unitHeads.begin(),area.unitHeads.end(),[](EntityId unit){return bool(unit);})) return false;
        for(const auto &[player,state]:ports_.players.all()) {
            (void)player;if(state.area==id) return false;
        }
        regions.push_back(id);
    }
    if(!discardPreparedRooms(regions)) return false;
    std::erase_if(state_.preparation,[&](const auto &request){return request.pd2Map==map;});
    std::erase_if(failed_,[&](const auto &entry){return entry.second.pd2Map==map;});
    for(const auto region:regions) {
        ports_.areas.areas_.erase(region);state_.residency.erase(region);
    }
    if(!regions.empty()) {link();invalidateRooms();refreshRoomTerrain();}
    return true;
}
void System::commitPd2Preparation(const std::shared_ptr<const pd2::MapInstance> &map) noexcept {
    for(auto &[id,area]:ports_.areas.areas_) {
        (void)id;if(area.definition.pd2Map==map) area.provisionalPd2Map=false;
    }
}
}

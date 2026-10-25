#include "../system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include <algorithm>

namespace d2x::server::world {
bool System::cancelPd2Preparation(const std::shared_ptr<const pd2::MapInstance> &map) {
    if(!map) return false;
    // A materialized DRLG has shared room/tile state and cannot be erased as
    // if it were an async job. Only cancel an instance not installed anywhere.
    for(const auto &[id,area]:ports_.areas.all()) {
        (void)id;if(area.definition.pd2Map==map) return false;
    }
    std::erase_if(state_.preparation,[&](const auto &request){return request.pd2Map==map;});
    // A failed content handoff must not permanently occupy this map's level.
    failed_.erase(RegionId(map->destination.level));
    return true;
}
}

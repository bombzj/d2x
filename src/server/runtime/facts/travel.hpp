#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "server/player_identity.hpp"
#include <vector>
#include <cstdint>

namespace d2x::server {
struct RepositionFact { EntityId actor; RegionId area; Vec position; bool notifyOwner=true; };
struct TravelFact { PlayerId player; EntityId actor; RegionId from, to; uint64_t areaGeneration{}; Vec position; bool walking{}, revived{}; };
struct WaypointFact { EntityId source; std::vector<RegionId> unlocked; };
}

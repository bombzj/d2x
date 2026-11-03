#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <vector>
#include <cstdint>

namespace d2x::server {
struct StateFact { EntityId actor; uint8_t type{}; RegionId area; int state{}; bool enabled{}; std::vector<StatValue> stats{}; };
struct AttributeFact { EntityId unit; uint64_t revision{}; };
struct OverlayFact { EntityId actor; uint8_t type{}; RegionId area; int overlay{}; };
}

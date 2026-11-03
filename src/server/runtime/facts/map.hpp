#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <vector>
#include <cstdint>

namespace d2x::server {
struct Pd2MapAttributesFact {EntityId portal;RegionId area;uint32_t itemIndex{};uint8_t quality{};std::vector<StatValue> attributes;};
}

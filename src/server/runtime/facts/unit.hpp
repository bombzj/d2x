#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <cstdint>
#include <array>

namespace d2x::server {
struct SoundFact { EntityId actor; uint8_t type{}; RegionId area; uint16_t sound{}; bool directSound{}; };
struct UnitRefreshFact { EntityId actor; uint8_t type{}; RegionId area; Vec position; };
struct MonsterClassHintFact {EntityId actor;RegionId area;uint16_t nativeClass{};};
struct MonsterIdentityFact {EntityId actor;RegionId area;uint16_t nameSeed{};std::array<uint8_t,3> modifiers{};bool champion{};};
}

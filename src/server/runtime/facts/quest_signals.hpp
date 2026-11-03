#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "gameplay/quest/id.hpp"
#include <array>
#include <cstdint>

namespace d2x::server {
struct QuestClueFact { uint16_t quest{}; std::array<uint16_t,6> payload{}; };
struct QuestStatusFact {QuestId quest;uint8_t status{};uint16_t counter{};};
struct QuestEffectFact {uint8_t effect{};};
struct QuestAlertFact {EntityId actor;RegionId area;uint8_t type{1};};
}

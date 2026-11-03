#pragma once
#include "core/entity_id.hpp"
#include <cstdint>
#include <vector>
#include <utility>

namespace d2x::server {
struct NpcMessage { uint8_t menu{}; uint16_t text{}; };
struct NpcMessagesFact { EntityId npc; std::vector<NpcMessage> messages; uint8_t type=1; bool activate=true; };
struct MerchantFact { EntityId npc, item; uint8_t operation{}, result{}; uint32_t gold{}; bool refreshShop{}; };
struct HirelingListFact { EntityId npc; std::vector<std::pair<uint16_t,uint32_t>> offers; };
struct NpcServiceFact { EntityId npc; uint8_t result{}, state{}; };
}

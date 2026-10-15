#pragma once
#include "server/game_messages.hpp"
#include "server/runtime/equipment_rules.hpp"
#include "world/identity.hpp"
#include "gameplay/items/state.hpp"
#include <cstdint>
#include <map>

namespace d2x::server::items {
struct State {
    InventoryState world;
    EquipmentRules equipment;
    uint64_t revision = 1;
    std::map<EntityId, PlayerId> goldOwners;
};
}

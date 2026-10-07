#pragma once
#include "server/movement.hpp"
#include "prepared_rules.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/attributes/system.hpp"
#include "server/systems/crafting/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/population/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/ai/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/death/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/merchant/system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/progression/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/social/system.hpp"
#include "server/systems/trade/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/replication/system.hpp"

namespace d2x::server {
// Lifetime owner only. Systems receive narrow Ports, never this whole registry.
struct GameSystems {
    GameSystems(PlayerStore &, AreaStore &, EntityIds &, uint64_t &random,
                EventOutbox &, const PreparedRules &, const GameSettings &);
    GameSystems(const GameSystems &) = delete;
    GameSystems &operator=(const GameSystems &) = delete;
    MovementSystem movement;
#define D2X_SYSTEM(id, member, phase, scope) member::System member;
#include "subsystems.inc"
#undef D2X_SYSTEM
};
}

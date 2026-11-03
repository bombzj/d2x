#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/items/handle.hpp"

namespace d2x::server::companions {
// Ownership and control for hirelings/summons; live actors belong to Monsters.
enum class Action { Hire, Resurrect, Dismiss, List, Equipment, Potion, CommandInteract, CommandMove };
struct Request { Action action; EntityId npc; std::optional<uint32_t> offer; std::optional<ItemHandle> item{}; bool belt{}; UnitTarget target{}; std::optional<Vec> destination{}; };
}

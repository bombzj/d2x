#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/items/handle.hpp"

namespace d2x::server::merchant {
enum class Action { Open, Gamble, Buy, Sell, Repair, RepairAll, IdentifyAll, IdentifyBought };
struct Request { Action action; UnitTarget npc; std::optional<ItemHandle> item; uint64_t offerRevision{}; bool gamble{}, multibuy{}; };
}

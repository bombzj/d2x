#pragma once
#include "gameplay/npc/intents.hpp"
#include "gameplay/items/intents.hpp"

namespace d2x::server::crafting {
struct RewardItem { EntityId npc; ItemHandle item; };
struct Pd2SacrificeItem { EntityId altar;ItemHandle input; };
struct TransmogrifyItem { std::optional<ItemHandle> source; };
using Intent = std::variant<TransmuteCube, SocketItem, ImbueItem, SocketQuestItem, PersonalizeQuestItem, RewardItem,Pd2SacrificeItem,TransmogrifyItem>;
struct Request { Intent intent; };
}

#pragma once
#include "gameplay/quest/id.hpp"
#include "core/entity_id.hpp"
#include <optional>

namespace d2x::server::quests {
enum class Action { Refresh, ViewLog, Acknowledge, ClaimReward, ClaimRespec, StaffUpdate, ReadJournal, ReadAncientAltar, ReadClue, ReadFrozenAnya };
struct Request { Action action; QuestId quest; std::optional<EntityId> npc; std::optional<uint32_t> message; std::optional<EntityId> item{}; };
}

#pragma once
#include "server/runtime/contracts.hpp"
#include "server/systems/items/system.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/quest/id.hpp"

namespace d2x::server::quests {
enum class RewardKind { Bark, TranslateScroll, CainRing, Malus, Rogue };
// Content preparation is external to the authority. The token, actor and
// interaction lease survive output backpressure; no callback/MPQ crosses here.
struct Preparation {
    ActorContext actor;
    uint64_t token{}, seed{}, inventoryRevision{}, characterRevision{}, conversation{};
    EntityId source;
    RewardKind kind{};
    PersistentCharacter character;
    std::string classCode;
    int difficulty{};
};
struct Prepared {
    Preparation source;
    items::PreparedBatch items;
    std::optional<HirelingRecord> hireling;
    std::string deferred;
};
}

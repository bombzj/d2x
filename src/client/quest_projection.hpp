#pragma once
#include "contracts/quest.hpp"

namespace d2x {
struct ClassicData;
struct QuestDisplayState {
    std::optional<uint8_t> status;
    std::optional<uint16_t> playerFlags;
    std::optional<bool> available; // Explicit native 5E admission fact.
};
// Decoded private flags/status and revealed native admission availability.
// No hidden game-wide flags, inventory, seed or executor.
struct QuestProjectionInput {
    uint64_t revision{};
    EntityId actor;
    int currentAct{};
    std::array<QuestDisplayState, size_t(QuestId::Count)> entries;
    std::optional<unsigned> denRemaining;
    std::optional<unsigned> staffTombOffset;
};
QuestView projectQuestDisplay(const ClassicData &, const QuestProjectionInput &);
} // namespace d2x

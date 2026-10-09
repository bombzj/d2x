#pragma once
#include "contracts/quest.hpp"

namespace d2x {
struct ClassicData;
struct QuestDisplayState {
    std::optional<uint8_t> status;
    std::optional<uint16_t> playerFlags;
};
// Decoded owner facts only. No game-wide flags, inventory, seed or executor.
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

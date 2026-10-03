#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2x {
enum class QuestId : uint8_t {
    DenOfEvil,
    SistersBurialGrounds,
    SearchForCain,
    ForgottenTower,
    ToolsOfTheTrade,
    SistersToTheSlaughter,
    RadamentsLair,
    HoradricStaff,
    TaintedSun,
    ArcaneSanctuary,
    Summoner,
    SevenTombs,
    Count
};
using ActOneQuest = QuestId;
inline constexpr size_t actOneQuestCount = 6;

// Stage meanings belong to each quest's rule, not to the presentation layer.
// The compact, difficulty-local record also leaves room for multi-step quests.
struct QuestRecord {
    uint32_t stage = 0;
    uint32_t flags = 0;
};
using QuestBook = std::array<std::array<QuestRecord, size_t(QuestId::Count)>, 3>;
using ActOneQuestBook = QuestBook;

inline constexpr size_t questIndex(ActOneQuest quest) { return size_t(quest); }
inline constexpr unsigned questAct(QuestId quest) { return questIndex(quest) < 6 ? 1 : 2; }

enum class RadamentStage : uint32_t { Unstarted, Assigned, LeftTown, Slain, Rewarded };
enum class StaffStage : uint32_t { Unstarted, ArtifactsFound = 1, Assembled = 5, Submitted = 6 };
enum class SunStage : uint32_t { Unstarted, Darkness, Explained, AltarDestroyed, Confirmed };
enum class ArcaneStage : uint32_t { Unstarted, PalaceOpened, JerhynBriefed, Entered, JournalRead };
enum class SummonerStage : uint32_t { Unstarted, Encountered, Slain, Confirmed };
enum class TombsStage : uint32_t { Unstarted, Assigned, DurielSlain, TyraelRescued, JerhynConfirmed, PassageGranted };
inline constexpr uint32_t staffScrollExplained = 1;
inline constexpr uint32_t questCompletionStage(QuestId quest) {
    constexpr std::array<uint32_t, size_t(QuestId::Count)> stages{4, 4, 8, 4, 6, 5,
        uint32_t(RadamentStage::Rewarded), uint32_t(StaffStage::Submitted), uint32_t(SunStage::Confirmed),
        uint32_t(ArcaneStage::JournalRead), uint32_t(SummonerStage::Confirmed), uint32_t(TombsStage::PassageGranted)};
    return stages.at(questIndex(quest));
}
inline constexpr uint32_t radamentBookPending = 1;
inline constexpr uint32_t radamentBookUsed = 2;
inline bool radamentAdvance(QuestRecord &record, RadamentStage stage) {
    if (record.stage >= uint32_t(stage)) return false;
    record.stage = uint32_t(stage);
    if (stage == RadamentStage::Slain) record.flags |= radamentBookPending;
    return true;
}
} // namespace d2x

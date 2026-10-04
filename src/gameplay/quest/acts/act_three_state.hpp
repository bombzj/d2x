#pragma once
#include "gameplay/quest/state.hpp"

namespace d2x {
enum class GoldenBirdStage : uint32_t { Unstarted, Figurine, FigurineExplained, Bird, BirdExplained, Brewing, Rewarded };
inline constexpr uint32_t goldenBirdPotionPending = 1;
enum class GidbinnStage : uint32_t { Unstarted, Assigned, AltarLit, Acquired, Returned, Rewarded };
inline constexpr uint32_t gidbinnRingGranted = 1, gidbinnHirelingGranted = 2;
enum class KhalimStage : uint32_t { Unstarted, Assigned, Relics, Assembled, OrbSmashed };
inline constexpr uint32_t khalimEyeExplained = 1, khalimBrainExplained = 2, khalimHeartExplained = 4,
    khalimFlailExplained = 8, khalimWillExplained = 16, khalimAssigned = 32;
enum class LamTomeStage : uint32_t { Unstarted, Assigned, LeftTown, Acquired, Rewarded };
enum class TempleStage : uint32_t { Unstarted, Assigned, Travincal, CouncilSlain, Completed };
enum class GuardianStage : uint32_t { Unstarted, Assigned, Durance, Mephisto, Completed };
inline constexpr uint32_t guardianSpeechPending = 1;
inline constexpr int lamTomeAttributeReward = 5; // A3Q1_UnitIterate_AddStatPointReward.
inline int questAttributePoints(const QuestBook &book) {
    int points = 0;
    for (const auto &difficulty : book)
        if (difficulty[questIndex(QuestId::LamEsensTome)].stage == uint32_t(LamTomeStage::Rewarded)) points += lamTomeAttributeReward;
    return points;
}
// ItemMode.cpp's quest-item use handler adds 5120 in native 8-bit fixed point.
inline constexpr int goldenBirdLifeReward = 20;
inline int questBaseLife(const QuestBook &book) {
    int life = 0;
    for (const auto &difficulty : book) {
        const auto &bird = difficulty[questIndex(QuestId::GoldenBird)];
        if (bird.stage == uint32_t(GoldenBirdStage::Rewarded) && !(bird.flags & goldenBirdPotionPending))
            life += goldenBirdLifeReward;
    }
    return life;
}
} // namespace d2x

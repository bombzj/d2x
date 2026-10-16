#pragma once
#include "timing.hpp"
#include <optional>
#include <span>
#include <vector>
#include <cstdint>

namespace d2x {
struct MonsterActionTimingInput {
    int duration{}, release{}, fallbackRate{100};
    std::optional<AnimationTiming> animation;
    int actionFrame{-1}, startFrame{};
    bool casting{}, attackRate{true}, sequence{};
    int fasterCast{}, itemIAS{}, weaponSpeed{}, skillRate{}, otherRate{};
    std::span<const int> animationEvents;
    std::span<const int> releaseFrames;
};
struct MonsterActionTiming {
    uint64_t duration{}, release{};
    int speed{};
    std::vector<int> events;
};
MonsterActionTiming planMonsterActionTiming(const MonsterActionTimingInput &input);
struct MonsterAnimationCursor {int32_t frame{},remaining{};};
struct MonsterAnimationStep {uint8_t event{};bool ended{};};
// UNITS_StopSequence + IsAtEndOfFrameCycle. Unlike player timer expansion,
// a monster retains only the last nonzero flag crossed during this frame.
MonsterAnimationStep advanceMonsterAnimation(MonsterAnimationCursor &,int frames,int speed,int frameBonus,
    bool sequence,std::span<const uint8_t> flags);
}

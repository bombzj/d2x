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
}
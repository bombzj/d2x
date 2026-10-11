#pragma once
#include <vector>

namespace d2x {
struct MonsterAttackTiming {
    float duration = 0;
    float impact = 0;
    int frames = 0;
    int sequenceFrames = 0;
    std::vector<float> eventTimes = {};
    int speed{}, actionFrame{-1};
    std::vector<int> eventFrames{};
};
} // namespace d2x

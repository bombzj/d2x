#pragma once
#include "timing.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
struct WeaponAttackTiming {
    std::string mode;
    int frames{}, speed{}, actionFrame{}, startFrame{};
    bool sequence{};
    std::vector<uint8_t> events;
    int durationTicks() const;
    int actionTick() const;
};
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode);
int weaponSequenceTick(int frame, int speed);
template<class Frames>
std::vector<int> weaponSequenceHitTicks(const Frames &frames, int speed) {
    std::vector<int> result;
    for (std::size_t frame = 0; frame < frames.size(); ++frame)
        if (frames[frame].hit) result.push_back(weaponSequenceTick(int(frame), speed));
    return result;
}
}

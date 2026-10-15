#include "weapon_timing.hpp"
#include <algorithm>

namespace d2x {
int weaponSequenceTick(int frame, int speed) {
    return std::max(1, animationFrameTicks(frame, speed));
}
int WeaponAttackTiming::durationTicks() const {
    if (sequence) return std::max(1, animationFrameTicks(frames, speed));
    return AnimationTiming{frames, speed}.durationTicks(startFrame);
}
int WeaponAttackTiming::actionTick() const {
    return std::clamp(animationFrameTicks(std::max(0, actionFrame - startFrame), speed), 1, durationTicks());
}
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode) {
    if ((character != "ama" && character != "sor") || (mode != "a1" && mode != "a2")) return 0;
    if (weapon == "hth") return 1;
    return weapon == "1hs" || weapon == "1ht" || weapon == "stf" ||
           weapon == "2hs" || weapon == "2ht" ? 2 : 0;
}
}
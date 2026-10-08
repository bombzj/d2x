#include "attack_timing.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace d2x {
int WeaponAttackTiming::durationTicks() const {
    return std::max(1, ((frames - startFrame) * 256 + speed - 1) / speed - 1);
}
int WeaponAttackTiming::actionTick() const {
    return std::clamp(((actionFrame - startFrame) * 256 + speed - 1) / speed,
                      1, durationTicks());
}
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate) {
    if (itemIAS <= -120) throw std::invalid_argument("Item IAS exceeds the native supported range");
    const int rate = int(std::clamp<int64_t>(100 + int64_t(120) * itemIAS / (120LL + itemIAS) +
                              int64_t(skillRate) - baseWeaponSpeed, 15, 175));
    return int(std::clamp<int64_t>(int64_t(animationSpeed) * rate / 100, 1, 32767));
}
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode) {
    // UNITS_GetFrameBonus: Amazon/Sorceress skip initial A1/A2 frames.
    if ((character != "ama" && character != "sor") || (mode != "a1" && mode != "a2")) return 0;
    if (weapon == "hth") return 1;
    return weapon == "1hs" || weapon == "1ht" || weapon == "stf" ||
           weapon == "2hs" || weapon == "2ht" ? 2 : 0;
}
} // namespace d2x

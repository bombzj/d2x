#pragma once
#include <string>
#include <string_view>
namespace d2x {
// Pure subset of the original single-player weapon attack timing. No executor state.
struct WeaponAttackTiming {
    std::string mode;
    int frames{}, speed{}, actionFrame{}, startFrame{};
    int durationTicks() const;
    int actionTick() const;
};
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate);
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode);
}

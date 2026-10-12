#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
namespace d2x {
// AnimData uses 8-bit fixed point; authority advances at 25 Hz.
struct AnimationTiming {
    int frames{}, speed{};
    int durationTicks(int startFrame = 0) const;
};
int effectiveFasterRate(int itemBonus);
int effectiveCastSpeed(int animationSpeed, int fasterCast);
int effectiveRecoverySpeed(int animationSpeed, int itemBonus, bool holyShield = false);
// Pure subset of the original single-player weapon attack timing. No executor state.
struct WeaponAttackTiming {
    std::string mode;
    int frames{}, speed{}, actionFrame{}, startFrame{};
    bool sequence{};
    int durationTicks() const;
    int actionTick() const;
};
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate);
int effectiveOtherAnimationSpeed(int animationSpeed, int otherRate);
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode);
int animationFrameTicks(int frame, int speed);
// Monster ENDANIM is scheduled one frame after the last mode event.
int monsterAnimationTicks(AnimationTiming, int startFrame = 0);
int weaponSequenceTick(int frame, int speed);
// SequenceTbls events share the same fixed-point clock for rendering and hits.
template<class Frames>
std::vector<int> weaponSequenceHitTicks(const Frames &frames, int speed) {
    std::vector<int> result;
    for (std::size_t frame = 0; frame < frames.size(); ++frame)
        if (frames[frame].hit) result.push_back(weaponSequenceTick(int(frame),speed));
    return result;
}
}

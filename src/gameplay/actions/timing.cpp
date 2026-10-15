#include "timing.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
int animationFrameTicks(int frame, int speed) {
    if (frame < 0 || speed <= 0) throw std::invalid_argument("Unprepared animation clock");
    const auto ticks = (int64_t(frame) * 256 + speed - 1) / speed;
    if (ticks > std::numeric_limits<int>::max()) throw std::overflow_error("Animation clock exceeds tick range");
    return int(ticks);
}
int AnimationTiming::durationTicks(int startFrame) const {
    if (frames <= 0 || startFrame < 0 || startFrame >= frames)
        throw std::invalid_argument("Unprepared animation frames");
    return std::max(1, animationFrameTicks(frames - startFrame, speed) - 1);
}
int monsterAnimationTicks(AnimationTiming timing, int startFrame) {
    if (timing.frames <= 0 || startFrame < 0 || startFrame >= timing.frames)
        throw std::invalid_argument("Unprepared monster animation frames");
    return std::max(2, animationFrameTicks(timing.frames - startFrame, timing.speed));
}
int effectiveFasterRate(int itemBonus) {
    if (itemBonus <= -120) throw std::invalid_argument("Animation bonus exceeds the native supported range");
    return nativeMultiplyDivide(itemBonus, 120, nativeAdd(120, itemBonus));
}
namespace {
int scaledAnimationSpeed(int speed, int rate) {
    if (speed <= 0) throw std::invalid_argument("Unprepared animation speed");
    return std::clamp(nativeMultiplyDivide(speed, rate, 100), 1, 32767);
}
}
int effectiveCastSpeed(int animationSpeed, int fasterCast) {
    return scaledAnimationSpeed(animationSpeed, std::min(nativeAdd(100, effectiveFasterRate(fasterCast)), 175));
}
int effectiveRecoverySpeed(int animationSpeed, int itemBonus, bool holyShield) {
    return scaledAnimationSpeed(animationSpeed, nativeAdd(holyShield ? 100 : 50, effectiveFasterRate(itemBonus)));
}
int effectiveOtherAnimationSpeed(int animationSpeed, int otherRate) {
    return scaledAnimationSpeed(animationSpeed, std::clamp(nativeAdd(100, otherRate), 15, 175));
}
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate) {
    const int rate = std::clamp(nativeSubtract(nativeAdd(nativeAdd(100,
        effectiveFasterRate(itemIAS)), skillRate), baseWeaponSpeed), 15, 175);
    return scaledAnimationSpeed(animationSpeed, rate);
}
}

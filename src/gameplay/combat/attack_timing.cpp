#include "attack_timing.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <limits>
namespace d2x {
int animationFrameTicks(int frame, int speed) {
    if (frame < 0 || speed <= 0) throw std::invalid_argument("Unprepared animation clock");
    const auto ticks=(int64_t(frame)*256+speed-1)/speed;
    if(ticks>std::numeric_limits<int>::max()) throw std::overflow_error("Animation clock exceeds tick range");
    return int(ticks);
}
int AnimationTiming::durationTicks(int startFrame) const {
    if (frames <= 0 || startFrame < 0 || startFrame >= frames)
        throw std::invalid_argument("Unprepared animation frames");
    return std::max(1, animationFrameTicks(frames-startFrame,speed)-1);
}
int monsterAnimationTicks(AnimationTiming timing, int startFrame) {
    if (timing.frames <= 0 || startFrame < 0 || startFrame >= timing.frames)
        throw std::invalid_argument("Unprepared monster animation frames");
    // SUnit::sub_6FCBCE70: ENDANIM = last MODECHANGE frame + 1,
    // with at least one mode tick before it. Do not borrow player rollback.
    return std::max(2, animationFrameTicks(timing.frames-startFrame,timing.speed));
}
int effectiveFasterRate(int itemBonus) {
    if (itemBonus <= -120) throw std::invalid_argument("Animation bonus exceeds the native supported range");
    return int(120LL*itemBonus/(120LL+itemBonus));
}
namespace {
int scaledAnimationSpeed(int speed, int64_t rate) {
    if (speed <= 0) throw std::invalid_argument("Unprepared animation speed");
    return int(std::clamp<int64_t>(int64_t(speed)*rate/100,1,32767));
}
}
int effectiveCastSpeed(int animationSpeed,int fasterCast) {
    return scaledAnimationSpeed(animationSpeed,std::min<int64_t>(100LL+effectiveFasterRate(fasterCast),175));
}
int effectiveRecoverySpeed(int animationSpeed,int itemBonus,bool holyShield) {
    return scaledAnimationSpeed(animationSpeed,(holyShield?100LL:50LL)+effectiveFasterRate(itemBonus));
}
int effectiveOtherAnimationSpeed(int animationSpeed,int otherRate) {
    return scaledAnimationSpeed(animationSpeed,std::clamp<int64_t>(100LL+otherRate,15,175));
}
int weaponSequenceTick(int frame, int speed) {
    return std::max(1,animationFrameTicks(frame,speed));
}
int WeaponAttackTiming::durationTicks() const {
    if(sequence) return std::max(1,animationFrameTicks(frames,speed));
    return AnimationTiming{frames,speed}.durationTicks(startFrame);
}
int WeaponAttackTiming::actionTick() const {
    return std::clamp(animationFrameTicks(std::max(0,actionFrame-startFrame),speed),
                      1, durationTicks());
}
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate) {
    const int rate = int(std::clamp<int64_t>(100LL + effectiveFasterRate(itemIAS) +
                              int64_t(skillRate) - baseWeaponSpeed, 15, 175));
    return scaledAnimationSpeed(animationSpeed,rate);
}
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode) {
    // UNITS_GetFrameBonus: Amazon/Sorceress skip initial A1/A2 frames.
    if ((character != "ama" && character != "sor") || (mode != "a1" && mode != "a2")) return 0;
    if (weapon == "hth") return 1;
    return weapon == "1hs" || weapon == "1ht" || weapon == "stf" ||
           weapon == "2hs" || weapon == "2ht" ? 2 : 0;
}
} // namespace d2x

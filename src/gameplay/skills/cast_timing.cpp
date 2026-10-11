#include "cast_timing.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
namespace d2x {
std::optional<CastAnimationTiming> prepareCastAnimationTiming(int frames, int speed, std::span<const uint8_t> events) {
    if (frames<=1 || frames>144 || speed<=0 || size_t(frames)>events.size()) return {};
    for (int frame=0;frame<frames;++frame)
        if (events[size_t(frame)]==1 || events[size_t(frame)]==2) return CastAnimationTiming{frames,speed,frame};
    return {};
}
std::optional<PlayerCastSequence> playerCastSequence(int sequence) {
    static constexpr std::array inferno{0,1,2,3,4,5,6,7,8,9,9,10,11,12,13};
    static constexpr std::array lightning{0,1,3,4,5,7,8,9,9,9,9,10,9,9,9,10,11,12,13};
    static constexpr std::array leap{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    if (sequence == 6) return PlayerCastSequence{inferno,10,10,256};
    if (sequence == 12) return PlayerCastSequence{lightning,7,-1,0};
    if (sequence == 13) return PlayerCastSequence{leap,5,10,256};
    return {};
}
CastTiming playerCastSequenceTiming(const PlayerCastSequence &sequence, int speed) {
    if (sequence.fixedSpeed) speed=sequence.fixedSpeed;
    if (speed <= 0 || sequence.frames.empty()) throw std::invalid_argument("Unprepared sequence timing");
    return {animationFrameTicks(int(sequence.frames.size()),speed),
        animationFrameTicks(sequence.releaseFrame,speed),speed};
}
CastTiming sorceressCastTiming(CastAnimationTiming animation, int fasterCast, bool arc, bool inferno) {
    if(inferno) {
        const auto sequence=*playerCastSequence(6);
        return playerCastSequenceTiming(sequence,sequence.fixedSpeed);
    }
    auto result=normalCastTiming(animation,fasterCast);
    if(arc) {
        const auto sequence=*playerCastSequence(12);
        result=playerCastSequenceTiming(sequence,result.speed);
    }
    return result;
}
CastTiming normalCastTiming(CastAnimationTiming animation, int fasterCast) {
    if (animation.frames <= 1 || animation.frames > 144 || animation.speed <= 0 ||
        animation.actionFrame < 0 || animation.actionFrame >= animation.frames)
        throw std::invalid_argument("Unprepared cast animation");
    // D2Common UNITS_UpdateCastAnimRateAndVelocity: OtherAnimRate is a separate mode branch.
    const int speed = effectiveCastSpeed(animation.speed,fasterCast);
    const int frames = AnimationTiming{animation.frames,speed}.durationTicks();
    return {frames, std::min(frames,animationFrameTicks(animation.actionFrame,speed)),speed};
}
}

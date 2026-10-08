#include "cast_timing.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace d2x {
CastTiming normalCastTiming(CastAnimationTiming animation, int fasterCast, int animationRate) {
    if (animation.frames <= 1 || animation.frames > 144 || animation.speed <= 0 ||
        animation.actionFrame < 0 || animation.actionFrame >= animation.frames)
        throw std::invalid_argument("Unprepared cast animation");
    const auto faster = std::max<int64_t>(0, fasterCast);
    const int rate = int(std::clamp<int64_t>(100 + 120 * faster / (120 + faster) + animationRate, 15, 175));
    const int speed = int(std::clamp<int64_t>(int64_t(animation.speed) * rate / 100, 1, 32767));
    const int frames = std::max(1, (animation.frames * 256 + speed - 1) / speed - 1);
    return {frames, std::min(frames, (animation.actionFrame * 256 + speed - 1) / speed), speed};
}
}

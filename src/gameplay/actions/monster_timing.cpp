#include "monster_timing.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
MonsterActionTiming planMonsterActionTiming(const MonsterActionTimingInput &input) {
    if (input.fallbackRate <= 0) throw std::invalid_argument("Unprepared monster action rate");
    const auto scaled = [&](int frames) {
        return uint64_t((int64_t(frames) * 100 + input.fallbackRate - 1) / input.fallbackRate);
    };
    MonsterActionTiming result{scaled(input.duration), scaled(input.release), 0, {}};
    if (input.animation) {
        const auto animation = *input.animation;
        if (input.casting) result.speed = effectiveCastSpeed(animation.speed, input.fasterCast);
        else if (input.attackRate) result.speed = effectiveAttackSpeed(animation.speed, input.itemIAS, input.weaponSpeed, input.skillRate);
        else if (input.sequence) result.speed = animation.speed;
        else result.speed = effectiveOtherAnimationSpeed(animation.speed, input.otherRate);
        result.duration = uint64_t(monsterAnimationTicks({animation.frames, result.speed}, input.startFrame));
        result.release = uint64_t(std::clamp(animationFrameTicks(std::max(0, input.actionFrame - input.startFrame), result.speed), 1, int(result.duration)));
    }
    if (result.speed && !input.animationEvents.empty()) {
        for (const int frame : input.animationEvents)
            result.events.push_back(std::max(1, animationFrameTicks(frame, result.speed)));
    } else {
        for (const int frame : input.releaseFrames) result.events.push_back(int(scaled(frame)));
    }
    return result;
}
}

#include "recovery.hpp"

namespace d2x {
AnimationTiming recoveryAnimationTiming(const RecoveryTimingInput &input) {
    auto timing = input.animation;
    if (input.mode == RecoveryMode::Knockback) {
        timing.speed = input.knockbackSpeed;
    } else {
        const bool block = input.mode == RecoveryMode::Block;
        timing.speed = effectiveRecoverySpeed(timing.speed,
            block ? input.fasterBlock : input.fasterHitRecovery, block && input.holyShield);
    }
    return timing;
}
}
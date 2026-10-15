#pragma once
#include "timing.hpp"

namespace d2x {
enum class RecoveryMode { GetHit, Block, Knockback };
struct RecoveryTimingInput {
    AnimationTiming animation;
    RecoveryMode mode{RecoveryMode::GetHit};
    int fasterHitRecovery{}, fasterBlock{}, knockbackSpeed{213};
    bool holyShield{};
};
AnimationTiming recoveryAnimationTiming(const RecoveryTimingInput &input);
}
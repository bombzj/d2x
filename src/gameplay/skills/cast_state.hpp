#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/effects/spec.hpp"

namespace d2x {
struct PendingSkillCast {
    SkillCastSpec skill;
    Vec target;
    int staticFieldMinimum = 0;
    float remaining = 0;
    EntityId enemy;
};
struct ChannelSkillCast {
    SkillCastSpec skill;
    Vec target;
    float remaining = 0;
    unsigned pulses = 0;
    float age = 0;
    EntityId enemy;
};
struct ThunderStormRuntime {
    EffectHandle effect;
    EffectFrame nextFrame = 0;
    EntityId lastTarget;
};
struct ChargeSkillState {
    SkillCastSpec skill;
    Vec target;
    EntityId enemy;
    float speed = 0;
    unsigned ticks = 0;
};
} // namespace d2x

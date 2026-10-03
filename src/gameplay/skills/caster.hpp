#pragma once
#include "gameplay/skills/cast_state.hpp"
#include <optional>

namespace d2x {
struct CombatEffectSet;
// Borrowed capabilities, valid for one synchronous execution only. They refer
// to the unit owner's existing storage; there is no second skill state copy.
struct SkillCaster {
    EntityId id;
    Vec &pos, &previous, &look;
    float &mana, &castTime, &lastCastDuration, &lastCastRate;
    EffectFrame &skillDelayUntil;
    bool &lightningSequence;
    std::optional<PendingSkillCast> &pendingCast;
    std::optional<ChannelSkillCast> &channel;
    std::optional<ThunderStormRuntime> &thunderStorm;
    CombatEffectSet &combatEffects;
    uint64_t &combatRandom;
    const bool &dead;
    bool blockAnimation, charge;
    const bool &moving;
    bool shield;
    const float &meleeTime, &hitTime;
};
} // namespace d2x

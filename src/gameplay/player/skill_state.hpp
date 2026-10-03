#pragma once
#include "gameplay/skills/cast_state.hpp"
#include "gameplay/skills/aura.hpp"
#include <optional>

namespace d2x {
struct PlayerSkills {
    EffectFrame skillDelayUntil = 0;
    float lastCastDuration = 0, lastCastRate = 0;
    bool lightningSequence = false;
    std::optional<PendingSkillCast> pendingCast;
    std::optional<ChannelSkillCast> channel;
    int channelSkill() const { return channel ? channel->skill.sourceId : -1; }
    float channelAge() const { return channel ? channel->age : 0; }
    std::optional<ActiveAura> aura;
    bool auraSuppressesManaRegen = false;
    std::optional<ThunderStormRuntime> thunderStorm;
};
} // namespace d2x

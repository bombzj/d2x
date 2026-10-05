#pragma once
#include "gameplay/skills/aura.hpp"
#include <optional>
namespace d2x {
// Ephemeral unit-owned skill state; native D2S stores type/experience only.
struct HirelingSkillState { std::optional<ActiveAura> aura;
    EffectFrame infernoEnd = 0, nextInfernoPulse = 0; };
}

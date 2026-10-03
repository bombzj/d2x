#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/effects/definition.hpp"

namespace d2x {
struct AuraDefinition;
struct SkillCorpse {
    EntityId id;
    Vec position;
    bool available = false;
};
struct SkillAuraSource {
    EntityId actor;
    const AuraDefinition *definition;
    EffectFrame *nextFrame;
};
} // namespace d2x

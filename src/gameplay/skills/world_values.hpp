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
struct CorpseExplosionSource {
    int64_t maximumLife = 0; // Native fixed-point mean unmodified life.
    int level = 0;
    uint64_t *random = nullptr;
};
struct SkillAuraSource {
    EntityId actor;
    const AuraDefinition *definition;
    EffectFrame *nextFrame;
};
} // namespace d2x

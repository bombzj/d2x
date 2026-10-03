#pragma once
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/combat/damage_type.hpp"

namespace d2x {
struct NativeSkillCast {
    SkillCastSpec skill;
    DamageType element;
    bool killOnHit = true;
};
} // namespace d2x

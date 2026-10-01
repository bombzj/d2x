#pragma once
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/aura.hpp"
#include <array>
#include <optional>
#include <vector>

namespace d2x {
using MonsterAura = AuraDefinition;
struct MonsterEnchantment {
    std::vector<int> ids;
    uint16_t nameSeed = 0;
    int level = 1, levelBonus = 3, experienceFactor = 5;
    int lifePercent = 0, lifeScalePercent = 100;
    int damagePercent = 0, attackRatingPercent = 0, defensePercent = 0, velocityPercent = 0;
    std::array<int, 6> resistances{}; // Resolved in modifier order, with the native immunity limit.
    std::array<AttackDamageRange, 6> elements{};
    AttackDamageRange manaDamage;
    int coldFrames = 0, poisonFrames = 0;
    AttackDamageRange spectralDamage;
    std::optional<MonsterAura> aura;
    std::optional<MonsterAura> curse;
    float corpseExplosionMinimum = 0, corpseExplosionMaximum = 0;
    bool melee = false, stopRegeneration = true;
    bool skillEffectsEnabled = true;
    bool has(int id) const {
        for (int value : ids) if (value == id) return true;
        return false;
    }
};
} // namespace d2x

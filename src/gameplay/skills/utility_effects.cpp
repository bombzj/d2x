#include "gameplay/combat/damage_request.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::releaseTelekinesis(SkillCaster player, const SkillCastSpec &skill, Vec, EntityId targetUnit) {
    if (!world_.telekinesis(targetUnit, skill.telekinesisRange, true)) return;
    const auto defender = combatUnit(targetUnit);
    if (defender.alive() && canAttack(player.id, defender.id)) {
        const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
        const float amount = float(minimum + limitedRandom(player.combatRandom,
            unsigned(std::max(0, maximum - minimum)))) / 256.f;
        const bool knockback = limitedRandom(player.combatRandom, 100) < unsigned(skill.telekinesisKnockbackChance);
        DamageRequest hit{player.id, defender.id, amount, MonsterDamageType::Lightning};
        hit.hitClass = 109;
        dealDamage(hit);
        if (knockback && combatUnit(defender.id).alive()) world_.knockback(player.id, defender.id);
    }
    emit(SkillActivated{skill.sourceId});
}

void SkillRuntime::releaseTeleport(SkillCaster player, const SkillCastSpec &, Vec target, EntityId) {
    world_.teleport(player.id, target);
}
} // namespace d2x

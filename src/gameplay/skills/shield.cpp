#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/rank_bonus.hpp"

namespace d2x {
float SkillRuntime::absorbEnergyShield(EntityId target, float amount) {
    auto defender = combatUnit(target);
    if (!defender || !defender.mana || !world_.hasResolver()) return amount;
    for (const auto &effect : defender.effects->entries()) {
        if (effect.spec.state.id != world_.energyShieldState() || !effect.activeAt(world_.frame())) continue;
        const auto shield = world_.resolve(target, effect.spec.source.definition, effect.spec.source.level);
        const auto absorbed = absorbSkillShield(amount, *defender.mana, shield.shieldPercent, shield.shieldManaFactor);
        amount = absorbed.damage;
        *defender.mana = absorbed.mana;
        const auto handle = effect.handle;
        if (absorbed.mana == 0) world_.effectsChanged(defender.effects->remove(handle));
        break;
    }
    return amount;
}
} // namespace d2x

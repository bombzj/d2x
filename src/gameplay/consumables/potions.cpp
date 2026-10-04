#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
float potionRestorationAmount(const PotionDefinition &potion, std::string_view playerClass) {
    // D2Common ITEMS_GetBonusLifeBasedOnClass / ITEMS_GetBonusManaBasedOnClass.
    // pSpell03 shifts recovery values to 8.8 before the x1.5 operation.
    if (potion.kind == PotionKind::Healing) {
        if (playerClass.empty() || playerClass == "bar") return potion.amount * 2.f;
        if (playerClass == "ama" || playerClass == "pal" || playerClass == "ass") return potion.amount * 1.5f;
    } else if (potion.kind == PotionKind::Mana) {
        if (playerClass == "sor" || playerClass == "nec" || playerClass == "dru") return potion.amount * 2.f;
        if (playerClass == "ama" || playerClass == "pal" || playerClass == "ass") return potion.amount * 1.5f;
    }
    return potion.amount;
}
void Simulation::applyPotion(const PotionDefinition &potion) {
    auto &p = state_.player;
    switch (potion.kind) {
    case PotionKind::Healing:
        p.resources.healing.push_back({potion.amount, potion.amount / potion.seconds});
        break;
    case PotionKind::Mana:
        p.resources.manaRestoration.push_back({potion.amount, potion.amount / potion.seconds});
        break;
    case PotionKind::Rejuvenation:
        p.resources.hp = std::min(float(state_.player.attributes.maxLife), p.resources.hp + state_.player.attributes.maxLife * potion.amount);
        p.resources.mana = std::min(float(state_.player.attributes.maxMana), p.resources.mana + state_.player.attributes.maxMana * potion.amount);
        break;
    case PotionKind::Stamina:
        p.resources.stamina = state_.player.attributes.maxStamina;
        break;
    case PotionKind::Remedy: break;
    }
    // Bridge the legacy ailment timers from the imported cure-state columns,
    // not from the potion's item identity. New effects use removeState below.
    if (potion.curesPoison) p.resources.poisonRemaining = p.resources.poisonPerSecond = 0;
    if (potion.curesCold) p.resources.chill = 0;
    if (potion.state.id >= 0) {
        for (int cured : potion.cureStates)
            if (cured >= 0) combatEffectsChanged(p.combatEffects.removeState(cured));
        EffectFrame duration = potion.durationFrames;
        for (const auto &existing : p.combatEffects.entries())
            if (existing.spec.state.id == potion.state.id && existing.expiresAt &&
                *existing.expiresAt > state_.frame)
                duration += *existing.expiresAt - state_.frame;
        CombatEffectSpec effect;
        effect.state = potion.state;
        effect.source = {CombatEffectSource::Item, p.id, potion.state.id, 0};
        effect.duration = duration;
        effect.modifiers = potion.modifiers;
        const auto applied = p.combatEffects.apply(std::move(effect), state_.frame);
        combatEffectsChanged(applied.removed);
    }
}
} // namespace d2x

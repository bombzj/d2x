#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::applyPotion(const PotionDefinition &potion) {
    auto &p = state_.player;
    switch (potion.kind) {
    case PotionKind::Healing:
        p.healing.push_back({potion.amount, potion.amount / potion.seconds});
        break;
    case PotionKind::Mana:
        p.manaRestoration.push_back({potion.amount, potion.amount / potion.seconds});
        break;
    case PotionKind::Rejuvenation:
        p.hp = std::min(float(characterStats_.maxLife), p.hp + characterStats_.maxLife * potion.amount);
        p.mana = std::min(float(characterStats_.maxMana), p.mana + characterStats_.maxMana * potion.amount);
        break;
    case PotionKind::Stamina:
        p.stamina = characterStats_.maxStamina;
        break;
    case PotionKind::Remedy: break;
    }
    // Bridge the legacy ailment timers from the imported cure-state columns,
    // not from the potion's item identity. New effects use removeState below.
    if (potion.curesPoison) p.poisonRemaining = p.poisonPerSecond = 0;
    if (potion.curesCold) p.chill = 0;
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
void Simulation::updatePotions(float dt) {
    auto &p = state_.player;
    auto restore = [dt](std::deque<Restoration> &queue, float &value, float maximum) {
        float remaining = dt;
        while (!queue.empty() && remaining > 0) {
            auto &effect = queue.front();
            float amount = std::min(effect.remaining, remaining * effect.rate);
            remaining -= amount / effect.rate;
            effect.remaining -= amount;
            value = std::min(maximum, value + amount);
            if (effect.remaining <= .0001f)
                queue.pop_front();
        }
        // Reaching full wastes the unused restoration, matching consumable behavior.
        if (value >= maximum)
            queue.clear();
    };
    restore(p.healing, p.hp, float(characterStats_.maxLife));
    restore(p.manaRestoration, p.mana, float(characterStats_.maxMana));
}
} // namespace d2x

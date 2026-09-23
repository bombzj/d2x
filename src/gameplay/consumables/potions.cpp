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
        p.staminaBoost += potion.seconds;
        break;
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
    if (p.staminaBoost > 0) {
        p.staminaBoost = std::max(0.f, p.staminaBoost - dt);
        p.stamina = characterStats_.maxStamina;
    }
}
} // namespace d2x

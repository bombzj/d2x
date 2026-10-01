#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void Simulation::updateAuras() {
    auto pulse = [&](CombatUnit source, const AuraDefinition &aura, EffectFrame &nextFrame) {
        if (!source.alive() || !active(*source.position) || state_.frame < nextFrame) return;
        const EffectFrame period = EffectFrame(std::max(5, aura.periodFrames));
        nextFrame = state_.frame + period;
        const EffectFrame duration = nextFrame - state_.frame + 1;
        auto apply = [&](CombatUnit target, const CombatStateDefinition &state, bool owner) {
            if (state.id < 0) return;
            for (const auto &effect : target.effects->entries())
                if (effect.activeAt(state_.frame) && effect.spec.state.id == state.id &&
                    effect.spec.source.definition == aura.skill && effect.spec.source.level > aura.rank) return;
            CombatEffectSpec effect;
            effect.state = state;
            effect.source = {CombatEffectSource::Skill, source.id, aura.skill, aura.rank};
            effect.duration = duration;
            if (!aura.hostile || !owner) effect.modifiers = aura.modifiers;
            if (aura.skill == 123 && !owner && target.monster && !target.hireling) {
                auto reduce = [&](int &value, MonsterDamageType type) {
                    const auto base = target.monster->intrinsicCombat
                        ? std::optional<int>{unitResistance(target, type)} : monsterResistance_
                        ? monsterResistance_(*target.monster, state_.area.region, type) : std::nullopt;
                    if (base && *base >= 100 && value < 0) value /= 5;
                };
                reduce(effect.modifiers.fireResist, MonsterDamageType::Fire);
                reduce(effect.modifiers.coldResist, MonsterDamageType::Cold);
                reduce(effect.modifiers.lightningResist, MonsterDamageType::Lightning);
            }
            if (owner) effect.modifiers.combat.damagePercent += aura.ownerDamageBonus;
            const auto removed = target.effects->apply(std::move(effect), state_.frame).removed;
            if (target.player) combatEffectsChanged(removed);
        };
        apply(source, aura.ownerState, true);
        for (auto target : combatUnits()) {
            if (!target.alive() || target.id == source.id || !rooms_->nearby(*source.position, *target.position)) continue;
            if (!aura.hostile && auraEligible_ && !auraEligible_(target)) continue;
            if (aura.hostile && safeZone_) continue;
            if ((aura.filter & 4) && !target.stats.undead) continue;
            if ((aura.filter & 0x4000) && target.stats.boss) continue;
            if ((aura.filter & 0x40000) && target.stats.primeEvil) continue;
            if ((aura.filter & 0x200) && !grid_->collisionSegment(*source.position, *target.position, 4)) continue;
            const float offsetX = std::floor(source.position->x) - std::floor(target.position->x);
            const float offsetY = std::floor(source.position->y) - std::floor(target.position->y);
            if (offsetX * offsetX + offsetY * offsetY > aura.radius * aura.radius) continue;
            if (aura.hostile ? !canAttack(source.id, target.id) : relation(source.id, target.id) != Relation::Allied) continue;
            apply(target, aura.state, false);
        }
    };
    if (state_.player.aura)
        pulse(combatUnit(state_.player.id), state_.player.aura->definition, state_.player.aura->nextFrame);
    for (auto &enemy : state_.area.enemies)
        if (enemy.identity.enchantment && enemy.identity.enchantment->aura &&
            (enemy.identity.enchantment->aura->skill == 98 || enemy.identity.enchantment->aura->skill == 108 ||
             enemy.identity.enchantment->aura->skill == 122 || enemy.identity.enchantment->aura->skill == 123))
            pulse(combatUnit(enemy.id), *enemy.identity.enchantment->aura, enemy.nextAuraFrame);
}
}
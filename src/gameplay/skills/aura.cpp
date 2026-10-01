#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <cmath>
#include "core/random.hpp"
#include "gameplay/monsters/monster_wander.hpp"

namespace d2x {
void Simulation::updateAuras() {
    auto pulse = [&](CombatUnit source, const AuraDefinition &aura, EffectFrame &nextFrame) {
        if (!source.alive() || !active(*source.position) || state_.frame < nextFrame) return;
        const EffectFrame period = EffectFrame(std::max(5, aura.periodFrames));
        nextFrame = state_.frame + period;
        const EffectFrame duration = nextFrame - state_.frame + 1;
        bool restoredLife = false;
        auto apply = [&](CombatUnit target, const CombatStateDefinition &state, bool owner) {
            if (state.id < 0) return;
            if (auraEligible_ && !auraEligible_(target, false)) return;
            if (aura.skill == 123 && !owner) {
                const AuraDefinition *ownAura = target.player && target.player->aura
                    ? &target.player->aura->definition : target.monster && target.monster->identity.enchantment &&
                        target.monster->identity.enchantment->aura ? &*target.monster->identity.enchantment->aura : nullptr;
                if (ownAura && ownAura->skill == aura.skill && ownAura->rank > aura.rank) return;
            }
            for (const auto &effect : target.effects->entries())
                if (effect.activeAt(state_.frame) && effect.spec.state.id == state.id &&
                    effect.spec.source.definition == aura.skill && effect.spec.source.level > aura.rank) return;
            const bool enoughMana = !source.player || *source.mana >= aura.manaPerPulse;
            if (enoughMana && aura.harmfulDurationPercent < 100) {
                target.effects->shortenCurableCurses(state_.frame, aura.harmfulDurationPercent);
                const int poisonFrames = std::max(0, int(*target.poisonTime * 25.f + .0001f));
                *target.poisonTime = float(poisonFrames * aura.harmfulDurationPercent / 100) / 25.f;
                if (*target.poisonTime == 0) *target.poisonRate = 0;
            }
            if (enoughMana && aura.lifePerPulse > 0 && *target.life < target.stats.attributes.maxLife) {
                const float before = *target.life;
                restoreUnit(target.id, aura.lifePerPulse);
                restoredLife |= *target.life > before;
            }
            CombatEffectSpec effect;
            effect.state = state;
            effect.source = {CombatEffectSource::Skill, source.id, aura.skill, aura.rank};
            effect.stacking = EffectStacking::AuraLevel;
            effect.duration = duration;
            if ((!aura.hostile || !owner) && enoughMana) effect.modifiers = aura.modifiers;
            if (owner) mergeCharacterModifiers(effect.modifiers, aura.ownerModifiers);
            if (aura.skill == 114 && !owner) {
                const int limit = unitColdEffect_ ? unitColdEffect_(target) : -50;
                effect.modifiers.velocityPercent = std::max(effect.modifiers.velocityPercent, limit);
                effect.modifiers.combat.attackRate = std::max(effect.modifiers.combat.attackRate, limit);
                effect.modifiers.otherAnimationRate = effect.modifiers.combat.attackRate;
            }
            if (aura.skill == 123 && !owner && target.monster && !target.hireling) {
                auto reduce = [&](int &value, MonsterDamageType type) {
                    const auto base = target.monster->intrinsicCombat
                        ? std::optional<int>{type == MonsterDamageType::Fire ? target.monster->intrinsicCombat->attributes.fireResist :
                            type == MonsterDamageType::Cold ? target.monster->intrinsicCombat->attributes.coldResist :
                            target.monster->intrinsicCombat->attributes.lightningResist} : monsterResistance_
                        ? monsterResistance_(*target.monster, state_.area.region, type) : std::nullopt;
                    if (base && *base >= 100 && value < 0) value /= 5;
                };
                reduce(effect.modifiers.fireResist, MonsterDamageType::Fire);
                reduce(effect.modifiers.coldResist, MonsterDamageType::Cold);
                reduce(effect.modifiers.lightningResist, MonsterDamageType::Lightning);
            }
            if (owner && source.player) effect.modifiers.combat.damagePercent += aura.ownerDamageBonus;
            if (aura.skill == 122 && !owner)
                for (const auto &existing : target.effects->entries())
                    if (existing.activeAt(state_.frame) && existing.spec.state.id == state.id &&
                        existing.spec.source.definition == aura.skill && existing.spec.source.level == aura.rank &&
                        existing.spec.source.entity == target.id) {
                        effect.modifiers.combat.damagePercent = existing.spec.modifiers.combat.damagePercent;
                        effect.source.entity = target.id;
                    }
            const auto removed = target.effects->apply(std::move(effect), state_.frame).removed;
            if (target.player) combatEffectsChanged(removed);
        };
        apply(source, aura.ownerState, true);
        if (aura.skill == 124) {
            if (safeZone_) return;
            for (auto &corpse : state_.area.enemies) {
                if (!corpse.corpseAvailable() || !rooms_->nearby(*source.position, corpse.pos) ||
                    !redemptionCorpseEligible_ || !redemptionCorpseEligible_(corpse)) continue;
                const auto deathDuration = monsterDeathDuration_ ? monsterDeathDuration_(corpse) : std::nullopt;
                if (!deathDuration || corpse.deathAge < *deathDuration) continue;
                const float offsetX = std::floor(source.position->x) - std::floor(corpse.pos.x);
                const float offsetY = std::floor(source.position->y) - std::floor(corpse.pos.y);
                if (offsetX * offsetX + offsetY * offsetY > aura.radius * aura.radius) continue;
                if (limitedRandom(*source.random, 100) >= unsigned(aura.redemptionChance)) continue;
                restoreUnit(source.id, aura.redemptionLife, aura.redemptionMana);
                corpse.corpseConsumed = true;
            }
            return;
        }
        float pulseDamage = 0;
        if (aura.hostile && aura.element >= 0 && !safeZone_) {
            const int minimum = int(aura.minimumDamage * 256.f), maximum = int(aura.maximumDamage * 256.f);
            pulseDamage = float(minimum + limitedRandom(*source.random, unsigned(std::max(0, maximum - minimum)))) / 256.f;
        }
        for (auto target : combatUnits()) {
            if (!target.alive() || target.id == source.id || !rooms_->nearby(*source.position, *target.position)) continue;
            if (!(aura.filter & (target.player ? 1 : 2))) continue;
            if ((aura.filter & 0x80) && !target.identity.attackable) continue;
            if (!aura.hostile && auraEligible_ && !auraEligible_(target, true)) continue;
            if (aura.hostile && safeZone_) continue;
            if ((aura.filter & 4) && !target.stats.undead) continue;
            if ((aura.filter & 0x4000) && target.stats.boss) continue;
            if ((aura.filter & 0x40000) && target.stats.primeEvil) continue;
            if ((aura.filter & 0x200) && !grid_->collisionSegment(*source.position, *target.position, 4)) continue;
            const float offsetX = std::floor(source.position->x) - std::floor(target.position->x);
            const float offsetY = std::floor(source.position->y) - std::floor(target.position->y);
            if (offsetX * offsetX + offsetY * offsetY > aura.radius * aura.radius) continue;
            if (aura.hostile ? !canAttack(source.id, target.id) : relation(source.id, target.id) != Relation::Allied) continue;
            if (aura.skill == 114 && (target.monster || target.hireling) &&
                (!unitColdEffect_ || unitColdEffect_(target) >= 0)) continue;
            apply(target, aura.state, false);
            if (pulseDamage > 0) {
                DamageRequest hit{source.id, target.id, pulseDamage, MonsterDamageType(aura.element), 0, false, false};
                hit.hitClass = aura.hitClass;
                hit.softHit = (aura.resultFlags & 0x4000) != 0;
                dealDamage(hit);
                if (aura.resultFlags & 8) applyAuraKnockback(source.id, target.id);
            }
            if (aura.skill == 114 && target.monster && target.alive()) {
                target.effects->removeState(shatterDeathState_.id);
                if (limitedRandom(*target.random, 100) < 20) {
                    CombatEffectSpec shatter;
                    shatter.state = shatterDeathState_;
                    shatter.source = {CombatEffectSource::Skill, source.id, aura.skill, aura.rank};
                    target.effects->apply(std::move(shatter), state_.frame);
                }
            }
        }
        if (source.player && aura.manaPerPulse > 0) {
            source.player->auraSuppressesManaRegen = restoredLife;
            if (restoredLife) *source.mana -= aura.manaPerPulse;
        }
    };
    if (state_.player.aura)
        pulse(combatUnit(state_.player.id), state_.player.aura->definition, state_.player.aura->nextFrame);
    for (auto &enemy : state_.area.enemies)
        if (enemy.identity.enchantment && enemy.identity.enchantment->aura &&
            (enemy.identity.enchantment->aura->skill == 98 || enemy.identity.enchantment->aura->skill == 102 || enemy.identity.enchantment->aura->skill == 108 ||
             enemy.identity.enchantment->aura->skill == 114 || enemy.identity.enchantment->aura->skill == 118 ||
             enemy.identity.enchantment->aura->skill == 122 || enemy.identity.enchantment->aura->skill == 123))
            pulse(combatUnit(enemy.id), *enemy.identity.enchantment->aura, enemy.nextAuraFrame);
}
void Simulation::applyAuraKnockback(EntityId attacker, EntityId defender) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source || !target.alive() || !target.monster) return;
    auto &enemy = *target.monster;
    const auto duration = monsterKnockbackDuration_ ? monsterKnockbackDuration_(enemy) : std::nullopt;
    if (!duration) { recoverUnit(defender, attacker, 1, true, 13, true); return; }
    const int offsetX = int(std::floor(enemy.pos.x)) - int(std::floor(source.position->x));
    const int offsetY = int(std::floor(enemy.pos.y)) - int(std::floor(source.position->y));
    const int divisor = std::max(std::abs(offsetX), std::abs(offsetY));
    Vec destination = enemy.pos;
    if (divisor > 0) destination = {std::floor(enemy.pos.x) + float(offsetX * 3 / divisor) + .5f,
        std::floor(enemy.pos.y) + float(offsetY * 3 / divisor) + .5f};
    monsterStopApproach(enemy);
    enemy.route.clear();
    enemy.attack = enemy.attackDuration = 0;
    enemy.attackImpact = -1;
    enemy.skill2Remaining = enemy.skill2Duration = 0;
    enemy.teleportTarget.reset();
    enemy.nestSpawnPosition.reset();
    enemy.knockbackRemaining = enemy.knockbackDuration = *duration;
    enemy.knockbackDestination = destination;
    enemy.knockbackFacing = *source.position - enemy.pos;
}
bool Simulation::advanceAuraKnockback(Enemy &enemy, float dt) {
    if (enemy.knockbackRemaining <= 0 || !enemy.knockbackDestination) return false;
    const Vec delta = *enemy.knockbackDestination - enemy.pos;
    const Vec next = enemy.pos + delta.unit() * std::min(delta.length(), 25.f * dt);
    if (grid_->segment(enemy.pos, next, {}, movementRule(enemy))) enemy.pos = next;
    else enemy.knockbackDestination = enemy.pos;
    enemy.knockbackRemaining = std::max(0.f, enemy.knockbackRemaining - dt);
    if (enemy.knockbackRemaining == 0) {
        enemy.knockbackDestination.reset();
        recoverUnit(enemy.id, {}, 1, false, 160, true);
    }
    return true;
}
void Simulation::reflectThorns(EntityId attacker, EntityId defender, float physicalDamage) {
    const auto source = combatUnit(attacker), target = combatUnit(defender);
    if (!source.alive() || !target.alive() || physicalDamage <= 0) return;
    int percent = target.stats.attributes.combat.thornsPercent;
    if (source.player || source.hireling) percent = (percent + 4) / 8;
    if (percent <= 0) return;
    const float reflected = float(int64_t(physicalDamage * 256.f) * percent / 100) / 256.f;
    DamageRequest hit{defender, attacker, reflected, MonsterDamageType::Physical, 0, false, false};
    hit.softHit = true;
    hit.hitClass = 0x8d;
    dealDamage(hit);
}
}
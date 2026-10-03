#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/projectile_source.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
namespace {
float rollDamage(Enemy &enemy, float minimum, float maximum) {
    const int low = int(minimum * 256.f), high = int(maximum * 256.f);
    return float(low + (high > low ? int(monsterAiRandom(enemy) % unsigned(high - low)) : 0)) / 256.f;
}
bool inRange(Vec origin, Vec target, float radius) {
    const float dx = std::floor(origin.x) - std::floor(target.x);
    const float dy = std::floor(origin.y) - std::floor(target.y);
    return dx * dx + dy * dy <= radius * radius;
}
}
void Simulation::updateMonsterEnchantments() {
    for (auto &enemy : state_.area.enemies) {
        enemy.combatEffects.expire(state_.frame);
        if (!enemy.enchantment || !active(enemy.pos)) continue;
        const auto &mods = *enemy.enchantment;
        if (enemy.pendingUniqueLightningFrame && state_.frame >= enemy.pendingUniqueLightningFrame) {
            enemy.pendingUniqueLightningFrame = 0;
            if (state_.frame >= std::max<EffectFrame>(10, enemy.nextUniqueLightningFrame)) {
                enemy.nextUniqueLightningFrame = state_.frame + 10;
                launchMonsterEnchantmentMissiles(enemy, 195);
            }
        }
        if (enemy.deathEnchantmentFrame && state_.frame >= enemy.deathEnchantmentFrame) {
            enemy.deathEnchantmentFrame = 0;
            if (mods.has(9)) {
                const int minimum = int(mods.corpseExplosionMinimum * 4.f);
                const int maximum = int(mods.corpseExplosionMaximum * 4.f);
                const float damage = float(minimum + limitedRandom(enemy.combatRandom,
                    unsigned(std::max(0, maximum - minimum)))) / 4.f;
                const int radius = state_.population.difficulty + 4;
                for (auto target : combatUnits())
                    if (target.player && target.alive() && canAttack(enemy.id, target.id) &&
                        rooms_->nearby(enemy.pos, *target.position) && inRange(enemy.pos, *target.position, float(radius)) &&
                        grid_->collisionSegment(enemy.pos, *target.position, 0x0805)) {
                        DamageRequest hit{enemy.id, target.id, damage, MonsterDamageType::Physical};
                        hit.channels[size_t(MonsterDamageType::Fire)] = damage;
                        dealDamage(hit);
                    }
                if (monsterSpecialMissile_)
                    if (const auto visual = monsterSpecialMissile_(117, 1))
                        state_.area.effects.push_back({enemy.pos, 0, visual->skill.missileLifetime, 117});
                emit(MissileReleased{117});
            }
            if (mods.has(18)) launchMonsterEnchantmentMissiles(enemy, 194);
        }
        if (!mods.skillEffectsEnabled) continue;
        if (enemy.hp <= 0 || !mods.aura || mods.aura->skill == 98 || mods.aura->skill == 102 || mods.aura->skill == 108 ||
            mods.aura->skill == 114 || mods.aura->skill == 118 || mods.aura->skill == 122 ||
            mods.aura->skill == 123 || state_.frame < enemy.nextAuraFrame) continue;
        skills().pulseLegacyAura(enemy.id, *mods.aura, enemy.nextAuraFrame);
    }
}
void Simulation::triggerMonsterLightning(Enemy &enemy) {
    if (!enemy.enchantment || !enemy.enchantment->has(17) ||
        state_.frame < std::max<EffectFrame>(10, enemy.nextUniqueLightningFrame)) return;
    enemy.nextUniqueLightningFrame = state_.frame + 10;
    launchMonsterEnchantmentMissiles(enemy, 195);
}
void Simulation::updateMonsterSpectralDamage(Enemy &enemy) {
    if (!enemy.enchantment || !enemy.enchantment->has(27)) return;
    auto &mods = *enemy.enchantment;
    constexpr int channels[]{2, 3, 1, 4, 5};
    const int channel = channels[monsterAiRandom(enemy) % 5];
    mods.elements[size_t(channel)] = mods.spectralDamage;
    if (channel == 4) mods.coldFrames += 40;
    if (channel == 5) mods.poisonFrames += 40;
}
void Simulation::prepareMonsterEnchantmentHit(Enemy &enemy, DamageRequest &hit, int sourceDamage) {
    if (!enemy.enchantment) return;
    const auto &mods = *enemy.enchantment;
    auto target = combatUnit(hit.defender);
    if (!target.alive()) return;
    const auto &stats = target.stats.attributes;
    auto elements = mods.elements;
    const auto &stateElements = enemy.combatEffects.modifiers(state_.frame).combat;
    elements[2].minimum += stateElements.fireMinimum;
    elements[2].maximum += stateElements.fireMaximum;
    elements[3].minimum += stateElements.lightningMinimum;
    elements[3].maximum += stateElements.lightningMaximum;
    elements[4].minimum += stateElements.coldMinimum;
    elements[4].maximum += stateElements.coldMaximum;
    elements[1].minimum += stateElements.magicMinimum;
    elements[1].maximum += stateElements.magicMaximum;
    const int coldFrames = mods.coldFrames + stateElements.coldFrames, poisonFrames = mods.poisonFrames;
    for (size_t channel = 1; channel < elements.size(); ++channel) {
        if (!elements[channel].maximum) continue;
        const float amount = rollDamage(enemy, float(elements[channel].minimum), float(elements[channel].maximum));
        const float scaled = float(int64_t(amount * 256.f) * sourceDamage / 128) / 256.f;
        if (channel == size_t(MonsterDamageType::Poison)) {
            applyPoison(hit.defender, scaled * 25.f / 256.f, float(poisonFrames) / 25.f, enemy.id);
        } else {
            hit.channels[channel] += scaled;
            if (channel == size_t(MonsterDamageType::Cold) && !stats.combat.cannotBeFrozen)
                hit.chill += float(coldFrames * sourceDamage / 128) / 25.f *
                    float(100 - stats.coldResist) / 100.f *
                    (stats.combat.halfFreezeDuration ? .5f : 1.f);
        }
    }
    if (target.mana && mods.manaDamage.maximum > 0)
        *target.mana = std::max(0.f, *target.mana -
            rollDamage(enemy, float(mods.manaDamage.minimum), float(mods.manaDamage.maximum)));
    if (mods.aura && mods.aura->element >= 0 && mods.aura->skill != 102 &&
        mods.aura->skill != 114 && mods.aura->skill != 118)
        hit.channels[size_t(mods.aura->element)] += float(int64_t(rollDamage(enemy,
            mods.aura->minimumDamage, mods.aura->maximumDamage) * mods.aura->elementalMultiplier * 256.f) *
            sourceDamage / 128) / 256.f;
}
void Simulation::applyMonsterCurse(Enemy &enemy, EntityId defender) {
    if (!enemy.enchantment || !enemy.enchantment->curse) return;
    skills().applyNativeCurse(enemy.id, defender, *enemy.enchantment->curse);
}
void Simulation::launchMonsterEnchantmentMissiles(Enemy &enemy, int missileId) {
    if (!monsterSpecialMissile_ || !enemy.enchantment) return;
    const int rank = std::max(1, enemy.enchantment->level / 2);
    const auto definition = monsterSpecialMissile_(missileId, rank);
    if (!definition) return;
    skills().releaseNativeBurst({enemy.id, enemy.pos, {}, enemy.combatRandom}, missileId, *definition);
}
bool Simulation::tryMonsterTeleport(Enemy &enemy) {
    if (!enemy.enchantment ||
        !enemy.enchantment->has(26) ||
        !combatUnit(enemy.combatTarget).alive() || enemy.hp <= 0 || safeZone_) return false;
    const auto &mods = *enemy.enchantment;
    const bool melee = mods.melee || enemy.kind == MonsterKind::Bighead;
    const auto targetUnit = combatUnit(enemy.combatTarget);
    const int targetDistance = monsterAiDistance(enemy.pos, targetUnit.stats.collisionSize, *targetUnit.position);
    if (monsterAiRandom(enemy) % 100 >= 40 ||
        (enemy.hp * 100 >= enemy.maxHp * 30 &&
            (melee || targetDistance >= 10)) ||
        monsterAiRandom(enemy) % 100 >= 15) return false;
    const auto *room = rooms_->room(enemy.pos);
    if (!room || room->width < 3 || room->height < 3) return false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const Vec target{float(room->x + 1 + monsterAiRandom(enemy) % unsigned(room->width - 1)) + .5f,
                         float(room->y + 1 + monsterAiRandom(enemy) % unsigned(room->height - 1)) + .5f};
        const int size = monsterSize_ ? monsterSize_(enemy) : 2;
        if (!grid_->walkable(target, {0x3c01, size}) ||
            !grid_->collisionSegment(enemy.pos, target, 0x0c01)) continue;
        bool occupied = false;
        for (const auto &other : combatUnits())
            if (other.id != enemy.id && other.alive() &&
                meleeDistance(target, size,
                    *other.position, other.stats.collisionSize) <= 0) occupied = true;
        if (occupied) continue;
        if (enemy.hp * 100 < enemy.maxHp * 30 && monsterAiRandom(enemy) % 100 < 25 &&
            !enemy.combatEffects.hasState(preventHealState_, state_.frame))
            enemy.hp = std::min(enemy.maxHp, enemy.hp + mods.level);
        const auto timing = monsterAttackTiming_ ? monsterAttackTiming_(enemy, 1) : std::nullopt;
        if (!timing || timing->duration <= 0) return false;
        enemy.teleportTarget = target;
        enemy.route.clear();
        enemy.aiPursuing = false;
        enemy.approach.reset();
        enemy.aiRunning = false;
        // The native monster teleport uses its A1 animation and action frame.
        enemy.attack = enemy.attackDuration = timing->duration;
        enemy.attackImpact = std::max(0.f, timing->impact);
        enemy.attackMode = 1;
        enemy.rethink = enemy.attackDuration;
        return true;
    }
    return false;
}
void Simulation::replicateMonsterMissile(const Enemy &enemy, Missile missile) {
    if (!enemy.enchantment ||
        !enemy.enchantment->has(29) ||
        noMultiShotMissiles_.contains(missile.missileId)) return;
    const auto sign = [](float value) { return value < 0 ? -1.f : value > 0 ? 1.f : 0.f; };
    const Vec target = monsterTargetPosition(enemy);
    const Vec aim{std::floor(target.x) + .5f, std::floor(target.y) + .5f};
    const Vec difference{std::floor(enemy.pos.x) - std::floor(target.x),
                         std::floor(enemy.pos.y) - std::floor(target.y)};
    const Vec side = unspreadMultiShotMissiles_.contains(missile.missileId) ? Vec{} :
        Vec{-sign(difference.y), sign(difference.x)};
    for (float direction : {-1.f, 1.f}) {
        auto copy = missile;
        copy.id = ids_.allocate();
        copy.combatRandom = childRandom(unitRandom_);
        copy.velocity = (aim + side * direction - copy.pos).unit() * missile.velocity.length();
        state_.area.missiles.push_back(std::move(copy));
    }
}
} // namespace d2x

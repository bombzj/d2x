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
        if (!enemy.identity.enchantment || !active(enemy.pos)) continue;
        const auto &mods = *enemy.identity.enchantment;
        if (enemy.pendingUniqueLightningFrame && state_.frame >= enemy.pendingUniqueLightningFrame) {
            enemy.pendingUniqueLightningFrame = 0;
            if (state_.frame >= enemy.nextUniqueLightningFrame) {
                enemy.nextUniqueLightningFrame = state_.frame + 10;
                launchMonsterEnchantmentMissiles(enemy, 195);
            }
        }
        if (enemy.deathEnchantmentFrame && state_.frame >= enemy.deathEnchantmentFrame) {
            enemy.deathEnchantmentFrame = 0;
            if (mods.has(9)) {
                const float damage = rollDamage(enemy, mods.corpseExplosionMinimum, mods.corpseExplosionMaximum);
                const int radius = state_.population.difficulty + 4;
                for (auto target : combatUnits())
                    if (target.alive() && canAttack(enemy.id, target.id) && inRange(enemy.pos, *target.position, float(radius))) {
                        dealDamage({enemy.id, target.id, damage, MonsterDamageType::Physical});
                        dealDamage({enemy.id, target.id, damage, MonsterDamageType::Fire});
                    }
                if (monsterSpecialMissile_)
                    if (const auto visual = monsterSpecialMissile_(117, 1))
                        state_.area.effects.push_back({enemy.pos, 0, visual->skill.missileLifetime, 117});
                emit(MissileReleased{117});
            }
            if (mods.has(18)) launchMonsterEnchantmentMissiles(enemy, 194);
        }
        if (enemy.hp <= 0 || !mods.aura || state_.frame < enemy.nextAuraFrame) continue;
        const auto &aura = *mods.aura;
        enemy.nextAuraFrame = state_.frame + EffectFrame(aura.periodFrames);
        auto apply = [&](CombatEffectSet &effects, bool owner) {
            if (aura.state.id < 0) return std::vector<RemovedCombatEffect>{};
            for (const auto &existing : effects.entries())
                if (existing.activeAt(state_.frame) && existing.spec.state.id == aura.state.id &&
                    existing.spec.source.level > aura.rank) return std::vector<RemovedCombatEffect>{};
            CombatEffectSpec effect;
            effect.state = aura.state;
            effect.source = {CombatEffectSource::Monster, enemy.id, aura.skill, aura.rank};
            effect.duration = EffectFrame(aura.periodFrames + 1);
            effect.modifiers = aura.modifiers;
            if (owner) effect.modifiers.combat.damagePercent += aura.ownerDamageBonus;
            return effects.apply(std::move(effect), state_.frame).removed;
        };
        if (aura.hostile) {
            if (aura.ownerState.id >= 0) {
                CombatEffectSpec ownerEffect;
                ownerEffect.state = aura.ownerState;
                ownerEffect.source = {CombatEffectSource::Monster, enemy.id, aura.skill, aura.rank};
                ownerEffect.duration = EffectFrame(aura.periodFrames + 1);
                enemy.combatEffects.apply(std::move(ownerEffect), state_.frame);
            }
        }
        for (auto target : combatUnits()) {
            if (!target.alive() || !rooms_->nearby(enemy.pos, *target.position) || !inRange(enemy.pos, *target.position, aura.radius)) continue;
            if (aura.hostile ? !canAttack(enemy.id, target.id) : relation(enemy.id, target.id) != Relation::Allied) continue;
            if (aura.state.id >= 0) {
                const auto removed = apply(*target.effects, target.id == enemy.id);
                if (target.player) combatEffectsChanged(removed);
            }
            if (aura.hostile && aura.element >= 0)
                dealDamage({enemy.id, target.id, rollDamage(enemy, aura.minimumDamage, aura.maximumDamage), MonsterDamageType(aura.element)});
        }
    }
}
void Simulation::applyMonsterEnchantmentHit(Enemy &enemy, EntityId defender, bool recovery) {
    if (!enemy.identity.enchantment) return;
    const auto &mods = *enemy.identity.enchantment;
    auto target = combatUnit(defender);
    if (!target.alive()) return;
    const auto &stats = target.stats.attributes;
    auto hurt = [&](float amount, MonsterDamageType type) { return dealDamage({enemy.id, defender, amount, type, 0, false, recovery}); };
    auto elements = mods.elements;
    int coldFrames = mods.coldFrames, poisonFrames = mods.poisonFrames;
    if (mods.has(27)) {
        constexpr int channels[]{2, 3, 1, 4, 5};
        const int channel = channels[monsterAiRandom(enemy) % 5];
        elements[channel].minimum += mods.spectralDamage.minimum;
        elements[channel].maximum += mods.spectralDamage.maximum;
        if (channel == 4) coldFrames += 40;
        if (channel == 5) poisonFrames += 40;
    }
    for (size_t channel = 1; channel < elements.size(); ++channel) {
        if (!elements[channel].maximum) continue;
        const float amount = rollDamage(enemy, float(elements[channel].minimum), float(elements[channel].maximum));
        if (channel == size_t(MonsterDamageType::Poison)) {
            applyPoison(defender, amount * 25.f / 256.f, float(poisonFrames) / 25.f, enemy.id);
        } else {
            hurt(amount, MonsterDamageType(channel));
            if (channel == size_t(MonsterDamageType::Cold) && !stats.combat.cannotBeFrozen)
                applyChill(defender, float(coldFrames) / 25.f *
                    float(100 - stats.coldResist) / 100.f *
                    (stats.combat.halfFreezeDuration ? .5f : 1.f));
        }
    }
    if (target.mana && mods.manaDamage.maximum > 0)
        *target.mana = std::max(0.f, *target.mana -
            rollDamage(enemy, float(mods.manaDamage.minimum), float(mods.manaDamage.maximum)));
    if (mods.aura && mods.aura->element >= 0)
        hurt(rollDamage(enemy, mods.aura->minimumDamage, mods.aura->maximumDamage) *
                   mods.aura->elementalMultiplier, MonsterDamageType(mods.aura->element));
    if (mods.curse && (monsterAiRandom(enemy) & 3) != 0) {
        auto curse = [&](CombatEffectSet &effects) {
            CombatEffectSpec effect;
            effect.state = mods.curse->state;
            effect.source = {CombatEffectSource::Monster, enemy.id, mods.curse->skill, mods.curse->rank};
            effect.duration = EffectFrame(mods.curse->periodFrames);
            effect.modifiers = mods.curse->modifiers;
            return effects.apply(std::move(effect), state_.frame).removed;
        };
        const float radius = std::clamp(mods.curse->radius, 1.f, 40.f);
        for (auto victim : combatUnits())
            if (victim.alive() && canAttack(enemy.id, victim.id) && inRange(enemy.pos, *victim.position, radius)) {
                const auto removed = curse(*victim.effects);
                if (victim.player) combatEffectsChanged(removed);
            }
    }
}
void Simulation::launchMonsterEnchantmentMissiles(Enemy &enemy, int missileId) {
    if (!monsterSpecialMissile_ || !enemy.identity.enchantment) return;
    const int rank = std::max(1, enemy.identity.enchantment->level / 2);
    const auto definition = monsterSpecialMissile_(missileId, rank);
    if (!definition) return;
    const auto *skill = &definition->skill;
    auto launch = [&](Vec heading, int index) {
        Missile missile{ids_.allocate(), enemy.id, enemy.pos, heading.unit() * skill->missileVelocity,
            skill->missileLifetime, skill->effect, false, missileId,
            rollDamage(enemy, skill->minimumDamage, skill->maximumDamage), 0, skill->coldDuration, true};
        missile.combatRandom = childRandom(unitRandom_);
        missile.fixedElement = definition->element;
        missile.killOnHit = definition->killOnHit;
        missile.nextHitDelay = skill->missileNextDelay;
        missile.acceleration = skill->missileAcceleration;
        missile.maxVelocity = skill->missileMaxVelocity;
        if (missileId == 195) {
            const auto path = chargedBoltPath(enemy.pos, enemy.pos + heading, index,
                                              int(skill->missileLifetime * 25.f));
            missile.path.assign(path.begin(), path.end());
        }
        state_.area.missiles.push_back(std::move(missile));
    };
    if (missileId == 195) {
        constexpr Vec directions[]{{0,-1},{1,0},{0,1},{-1,0}};
        for (auto heading : directions)
            for (int index = 0; index < 2; ++index) launch(heading, index);
    } else {
        constexpr int offsets[]{30,29,29,28,27,26,24,23,21,19,16,14,11,8,5,2,
            0,-2,-5,-8,-11,-14,-16,-19,-21,-23,-24,-26,-27,-28,-29,-29,
            -30,-29,-29,-28,-27,-26,-24,-23,-21,-19,-16,-14,-11,-8,-5,-2,
            0,2,5,8,11,14,16,19,21,23,24,26,27,28,29,29};
        for (int i = 0; i < 64; ++i) launch({float(offsets[i]), float(offsets[(i + 48) % 64])}, i);
    }
    emit(MissileReleased{missileId});
}
bool Simulation::tryMonsterTeleport(Enemy &enemy) {
    if (!enemy.identity.enchantment || !enemy.identity.enchantment->has(26) ||
        !combatUnit(enemy.combatTarget).alive() || enemy.hp <= 0) return false;
    const auto &mods = *enemy.identity.enchantment;
    if (monsterAiRandom(enemy) % 100 >= 40 ||
        (enemy.hp * 100 >= enemy.maxHp * 30 &&
            (mods.melee || (enemy.pos - monsterTargetPosition(enemy)).length() >= 10)) ||
        monsterAiRandom(enemy) % 100 >= 15) return false;
    const auto *room = rooms_->room(enemy.pos);
    if (!room || room->width < 3 || room->height < 3) return false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const Vec target{float(room->x + 1 + monsterAiRandom(enemy) % unsigned(room->width - 1)) + .5f,
                         float(room->y + 1 + monsterAiRandom(enemy) % unsigned(room->height - 1)) + .5f};
        if (!grid_->walkable(target, movementRule(enemy)) || (target - monsterTargetPosition(enemy)).length() < 2) continue;
        bool occupied = false;
        for (const auto &other : state_.area.enemies)
            if (other.id != enemy.id && other.hp > 0 && (other.pos - target).length() < 2) occupied = true;
        if (occupied) continue;
        if (enemy.hp * 100 < enemy.maxHp * 30 && monsterAiRandom(enemy) % 100 < 25)
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
    if (!enemy.identity.enchantment || !enemy.identity.enchantment->has(29) ||
        noMultiShotMissiles_.contains(missile.missileId)) return;
    const auto sign = [](float value) { return value < 0 ? -1.f : value > 0 ? 1.f : 0.f; };
    const Vec difference = enemy.pos - monsterTargetPosition(enemy);
    const Vec side = unspreadMultiShotMissiles_.contains(missile.missileId) ? Vec{} :
        Vec{-sign(difference.y), sign(difference.x)};
    for (float direction : {-1.f, 1.f}) {
        auto copy = missile;
        copy.id = ids_.allocate();
        copy.combatRandom = childRandom(unitRandom_);
        copy.velocity = (monsterTargetPosition(enemy) + side * direction - copy.pos).unit() * missile.velocity.length();
        state_.area.missiles.push_back(std::move(copy));
    }
}
} // namespace d2x

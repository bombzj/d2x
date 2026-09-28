#include "core/random.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
bool Simulation::firePhysicalProjectile(Vec target, const WeaponDamage &weapon, bool thrown,
                                        const SkillCastSpec *skill) {
    auto &player = state_.player;
    const auto selected = weapon;
    if (!selected.projectile || (thrown && !selected.throwable)) return false;
    if (skill && (!skill->weapon || (skill->weapon->manaOnRelease && player.mana < skill->manaCost))) {
        state_.message = "Not enough mana";
        return false;
    }
    const auto &spec = *selected.projectile;
    if (std::abs(target.x - player.pos.x) >= 100 || std::abs(target.y - player.pos.y) >= 100) return false;
    const bool potion = thrown && selected.potion;
    if (potion && spec.velocityUnits <= 0) return false;
    // Resolve the launch snapshot before removing the final equipped stack.
    const auto priorRandom = player.combatRandom;
    AttackElements elements;
    if (!potion) elements = rollAttackElements(selected.item, nullptr, skill);
    elements.ranged = true;
    const auto roll = [&](int minimum, int maximum) {
        if (maximum < minimum) std::swap(minimum, maximum);
        rollRandom(player.combatRandom);
        const auto span = uint32_t(std::max(0, maximum - minimum));
        return float(minimum + (span ? uint32_t(player.combatRandom) % span : 0)) / 256.f;
    };
    const float physical = potion ? 0 :
        roll(selected.projectileMinimum, selected.projectileMaximum);
    MissileImpactDamage impactDamage;
    if (potion)
        for (size_t channel = 0; channel < spec.damage.size(); ++channel)
            if (spec.damage[channel].maximum > 0)
                impactDamage.channels[channel] = roll(spec.damage[channel].minimum, spec.damage[channel].maximum);
    const int level = player.level;
    if (!spendProjectile_ || !spendProjectile_(selected.item, thrown)) {
        player.combatRandom = priorRandom;
        state_.message = thrown ? "No throwing weapon remains." : "Matching arrows or bolts are required.";
        return false;
    }
    const Vec direction = (target - player.pos).unit();
    Missile missile{ids_.allocate(), player.id, player.pos,
        direction * (skill ? skill->missileVelocity : spec.speed),
        skill ? skill->missileLifetime : spec.lifetime, SkillBehavior::None, true,
        skill ? skill->missileId : spec.id, physical};
    missile.attackElements = elements;
    missile.attackerLevel = level;
    missile.attackRating = selected.attackRating;
    missile.baseAttackRating = selected.baseAttackRating;
    missile.attackRatingPercent = selected.attackRatingPercent + (skill ? skill->weapon->attackRating : 0);
    missile.targetModifiers = selected.target;
    missile.playerAttack = true;
    missile.physicalDamagePercent = potion ? 0 : selected.projectileDamagePercent;
    missile.combatRandom = childRandom(unitRandom_);
    if (skill) {
        missile.skillId = skill->sourceId;
        missile.skillRank = skill->rank;
        missile.impact = skill->missileImpact;
        if (skill->weapon->manaOnRelease) player.mana = std::max(0.f, player.mana - skill->manaCost);
        if (skill->weapon->delayFrames > 0)
            player.skillDelayUntil = state_.frame + EffectFrame(skill->weapon->delayFrames);
    }
    if (potion) {
        missile.physical = false;
        missile.groundTargeted = spec.groundTargeted;
        missile.impact = spec.impact;
        missile.impactDamage = impactDamage;
        // MISSILES_CreateMissileFromParams, flag 0x400: integer distance changes
        // the remaining frames only; it does not rescale the original velocity.
        const int frames = std::max(1, int(int64_t(std::max(1, missileDistance(player.pos, target))) *
                                           4096 / spec.velocityUnits));
        missile.remaining = float(frames) / 25.f;
    }
    state_.area.missiles.push_back(std::move(missile));
    emit(MissileReleased{skill ? skill->missileId : spec.id});
    return true;
}
void Simulation::advancePhysicalMissile(Missile &missile, float dt, std::vector<Missile> &spawned) {
    Vec next = missile.pos + missile.velocity * std::min(dt, missile.remaining);
    const bool wall = clipMissilePath(missile.missileId, missile.pos, next);
    const float remaining = std::max(0.f, missile.remaining - dt);
    const bool expired = remaining <= .00001f;
    Enemy *struck = nullptr;
    float first = 2;
    const auto collision = missileCollisions_.find(missile.missileId);
    if (collision == missileCollisions_.end()) { missile.remaining = 0; return; }
    if (!expired)
        for (auto &enemy : state_.area.enemies) {
            if (enemy.hp <= 0 || !active(enemy.pos) || enemy.id == missile.lastHit) continue;
            const int size = monsterSize_ ? monsterSize_(enemy) : 0;
            if (auto at = missileUnitIntersection(missile.pos, next, collision->second.size, enemy.pos, size);
                at && *at < first) { first = *at; struck = &enemy; }
        }
    missile.pos = struck ? missile.pos + (next - missile.pos) * first : next;
    // Expiry precedes unit hits, and must set exactly zero so the update removes
    // the missile. A small positive residue must not detonate again next tick.
    missile.remaining = wall || struck || expired ? 0 : remaining;
    // AlwaysExplode runs the native hit effect on a failed to-hit roll, terrain and expiry too.
    if (missile.remaining == 0 && missile.impact) resolveMissileImpact(missile, spawned, struck);
    if (!struck) return;
    missile.lastHit = struck->id;
    const auto defense = monsterDefense_ ? monsterDefense_(*struck, state_.area.region) : std::nullopt;
    if (!defense) { state_.message = "Original monster defense is unavailable."; return; }
    if (missile.attackerLevel <= 0) return;
    rollRandom(missile.combatRandom);
    const int chance = (missile.playerAttack || !missile.attackElements.playerKillEffects) ? weaponHitChance(missile.attackerLevel,
        missile.baseAttackRating, missile.attackRatingPercent, missile.targetModifiers, *defense, struck->identity.rank) :
        physicalHitChance(missile.attackerLevel, missile.attackRating, defense->level, defense->defense);
    if (uint32_t(missile.combatRandom) % 100 >= unsigned(chance)) return;
    const int64_t raw = int64_t(missile.damage * 256.f);
    const int percent = std::max(-90, missile.physicalDamagePercent + targetDamageBonus(missile.targetModifiers, *defense));
    const float physical = float(raw + raw * percent / 100) / 256.f;
    resolveWeaponHit(*struck, physical, missile.owner, missile.attackElements);
}
} // namespace d2x

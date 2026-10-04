#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/missile_launch_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
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
    if (skill && (!skill->weapon || (skill->weapon->manaOnRelease && player.resources.mana < skill->manaCost))) {
        state_.message = "Not enough mana";
        return false;
    }
    const auto &spec = *selected.projectile;
    if (std::abs(target.x - player.movement.pos.x) >= 100 || std::abs(target.y - player.movement.pos.y) >= 100) return false;
    const bool potion = thrown && selected.potion;
    if (potion && spec.velocityUnits <= 0) return false;
    // Resolve the launch snapshot before removing the final equipped stack.
    const auto priorRandom = player.combatRandom;
    const auto launchCombat = player.attributes.combat;
    AttackElements elements;
    if (!potion) elements = rollAttackElements(selected.item, nullptr, skill);
    elements.ranged = true;
    elements.hitClass = selected.hitClass;
    const auto roll = [&](int minimum, int maximum) {
        if (maximum < minimum) std::swap(minimum, maximum);
        rollRandom(player.combatRandom);
        const auto span = uint32_t(std::max(0, maximum - minimum));
        return float(minimum + (span ? uint32_t(player.combatRandom) % span : 0)) / 256.f;
    };
    const auto scaleSource = [&](AttackElements &value, int source) {
        if (source != 128) {
            const auto scale = [&](float value) { return float(int64_t(value * 256.f) * source / 128) / 256.f; };
            value.fire = scale(value.fire); value.cold = scale(value.cold);
            value.lightning = scale(value.lightning); value.magic = scale(value.magic);
            value.poisonPerSecond = scale(value.poisonPerSecond / 25.f) * 25.f;
            value.coldDuration = float(int(value.coldDuration * 25.f) * source / 128) / 25.f;
            const auto &combat = launchCombat;
            WeaponModifiers own;
            if (auto found = combat.weapons.find(selected.item); found != combat.weapons.end()) own = found->second;
            const int count = (combat.poisonSources + own.poisonSources) * source / 128;
            value.poisonDuration = float((combat.poisonFrames + own.poisonFrames) / std::max(1, count)) / 25.f;
            value.lifeLeech = value.lifeLeech * source / 128;
            value.manaLeech = value.manaLeech * source / 128;
        }
    };
    float physical = potion ? 0 :
        roll(selected.projectileMinimum, selected.projectileMaximum);
    if (skill && skill->weapon->bow) {
        const auto &bow = *skill->weapon->bow;
        physical = float(int64_t(physical * 256.f) * bow.sourceDamage / 128) / 256.f;
        if (bow.physicalSkillDamage)
            physical += roll(int(skill->minimumDamage * 256.f), int(skill->maximumDamage * 256.f));
        else if (bow.element == DamageType::Fire)
            elements.fire += roll(int(skill->minimumDamage * 256.f), int(skill->maximumDamage * 256.f));
        else if (bow.element == DamageType::Cold) {
            elements.cold += roll(int(skill->minimumDamage * 256.f), int(skill->maximumDamage * 256.f));
            elements.coldDuration += skill->coldDuration;
        }
        elements.conversionPercent = bow.conversionPercent; elements.conversionElement = bow.element;
        elements.freezeFrames = int(elements.coldDuration * 25.f + .001f) * bow.freezePercent / 100;
        scaleSource(elements, bow.sourceDamage);
    }
    if (skill && skill->weapon->spear && skill->weapon->spear->kind == SpearSkillSpec::Kind::Bolt) {
        const auto &program = *skill->weapon->spear;
        physical = float(int64_t(physical * 256.f) * program.sourceDamage / 128) / 256.f;
        scaleSource(elements, program.sourceDamage);
        elements.lightning += roll(int(skill->minimumDamage * 256.f), int(skill->maximumDamage * 256.f));
        // SrvDmg12 preserves only the selected lightning channel and converted physical.
        elements.fire = elements.cold = elements.magic = elements.poisonPerSecond = elements.poisonDuration = elements.coldDuration = 0;
        elements.lifeLeech = elements.manaLeech = elements.freezeFrames = 0;
        elements.conversionPercent = program.conversionPercent; elements.conversionElement = DamageType::Lightning;
    }
    if (skill && skill->weapon->spear && skill->weapon->spear->kind == SpearSkillSpec::Kind::Fury)
        elements.lightning += roll(int(skill->minimumDamage * 256.f), int(skill->maximumDamage * 256.f));
    MissileImpactDamage impactDamage;
    if (potion)
        for (size_t channel = 0; channel < spec.damage.size(); ++channel)
            if (spec.damage[channel].maximum > 0)
                impactDamage.channels[channel] = roll(spec.damage[channel].minimum, spec.damage[channel].maximum);
    const int level = player.character.level;
    if (!(skill && skill->weapon->noAmmo) && (!spendProjectile_ || !spendProjectile_(selected.item, thrown))) {
        player.combatRandom = priorRandom;
        state_.message = thrown ? "No throwing weapon remains." : "Matching arrows or bolts are required.";
        return false;
    }
    const Vec direction = (target - player.movement.pos).unit();
    Missile missile{ids_.allocate(), player.id, player.movement.pos,
        direction * (skill ? skill->missileVelocity : spec.speed),
        skill ? skill->missileLifetime : spec.lifetime, SkillBehavior::None, true,
        skill ? skill->missileId : spec.id, physical};
    missile.attackElements = elements;
    missile.attackerLevel = level;
    missile.attackRating = selected.attackRating;
    missile.baseAttackRating = selected.baseAttackRating;
    missile.attackRatingPercent = selected.attackRatingPercent + (skill ? skill->weapon->attackRating : 0);
    missile.targetModifiers = selected.target;
    missile.weaponAttack = true;
    missile.physicalDamagePercent = potion ? 0 : selected.projectileDamagePercent + (skill ? skill->weapon->damagePercent : 0);
    missile.combatRandom = childRandom(unitRandom_);
    if (skill) {
        missile.skillId = skill->sourceId;
        missile.skillRank = skill->rank;
        missile.hitOverlayId = skill->hitOverlayId; missile.hitOverlayDuration = skill->hitOverlayDuration;
        if (skill->weapon->spear) {
            missile.spear = std::make_shared<SpearMissileState>();
            missile.spear->program = skill->weapon->spear;
        }
        missile.impact = skill->missileImpact;
        if (skill->weapon->bow && skill->weapon->bow->immolation)
            missile.impactDamage.channels[size_t(DamageType::Fire)] = elements.fire;
        missile.nextHitDelay = skill->missileNextDelay;
        if (skill->weapon->bow && selected.weaponClass == "xbw" && skill->weapon->bow->boltId >= 0)
            missile.missileId = skill->weapon->bow->boltId;
        if (skill->weapon->bow && skill->weapon->bow->guided) {
            const auto &bow = *skill->weapon->bow;
            auto guidance = std::make_shared<BoneSkillSpec>();
            guidance->spirit = true; guidance->retargetPeriod = bow.retargetPeriod;
            guidance->searchRadius = bow.searchRadius; guidance->spiritLifetime = skill->missileLifetime;
            missile.bone = std::make_shared<BoneMissileState>();
            auto &state = *missile.bone; state.program = std::move(guidance);
            state.target = player.actions.weaponAttack ? player.actions.weaponAttack->target : EntityId{};
            state.coordinateTarget = !state.target;
            state.offset = {float(int(target.x) - int(player.movement.pos.x)), float(int(target.y) - int(player.movement.pos.y))};
            if (state.coordinateTarget)
                missile.remaining = float(std::max(1, int(float(std::max(1, missileDistance(player.movement.pos, target))) *
                    25.f / skill->missileVelocity))) / 25.f;
        }
        if (skill->weapon->manaOnRelease) player.resources.mana = std::max(0.f, player.resources.mana - skill->manaCost);
        if (skill->weapon->delayFrames > 0)
            player.skills.skillDelayUntil = state_.frame + EffectFrame(skill->weapon->delayFrames);
    }
    if (potion) {
        missile.physical = false;
        missile.groundTargeted = spec.groundTargeted;
        missile.impact = spec.impact;
        missile.impactDamage = impactDamage;
        // MISSILES_CreateMissileFromParams, flag 0x400: integer distance changes
        // the remaining frames only; it does not rescale the original velocity.
        const int frames = std::max(1, int(int64_t(std::max(1, missileDistance(player.movement.pos, target))) *
                                           4096 / spec.velocityUnits));
        missile.remaining = float(frames) / 25.f;
    }
    if (skill && skill->weapon->bow && skill->weapon->bow->multiple) {
        int dx = int(target.x) - int(player.movement.pos.x), dy = int(target.y) - int(player.movement.pos.y);
        if (!dx && !dy) { dx = int(std::round(player.movement.look.x * 4)); dy = int(std::round(player.movement.look.y * 4)); }
        if (dx * dx + dy * dy < 4) { dx *= 4; dy *= 4; }
        if (dx * dx + dy * dy < 16) { dx *= 2; dy *= 2; }
        int sideX = dy, sideY = -dx;
        while (sideX * sideX + sideY * sideY > 3) { sideX /= 2; sideY /= 2; }
        const int count = skill->missileCount;
        Vec endpoint{float(int(target.x) - count * sideX / 2), float(int(target.y) - count * sideY / 2)};
        const Vec origin{float(int(player.movement.pos.x)) + .5f, float(int(player.movement.pos.y)) + .5f};
        const int central = std::min(count, skill->weapon->bow->centralArrows), first = (count - central) / 2;
        for (int index = 0; index < count; ++index) {
            auto arrow = missile; arrow.id = ids_.allocate(); arrow.pos = origin;
            auto heading = (endpoint + Vec{.5f,.5f} - origin).unit();
            if (heading.length() == 0) heading = player.movement.look;
            arrow.velocity = heading * skill->missileVelocity; arrow.combatRandom = childRandom(unitRandom_);
            const int base = selected.projectileMinimum + int(limitedRandom(arrow.combatRandom,
                unsigned(std::max(0, selected.projectileMaximum - selected.projectileMinimum))));
            arrow.damage = float(int64_t(base) * skill->weapon->bow->sourceDamage / 128) / 256.f;
            arrow.attackElements = rollAttackElements(selected.item, &launchCombat, skill, &arrow.combatRandom);
            arrow.attackElements.ranged = true;
            arrow.attackElements.hitClass = selected.hitClass;
            scaleSource(arrow.attackElements, skill->weapon->bow->sourceDamage);
            if (index < first || index >= first + central) {
                arrow.attackElements.crushing = arrow.attackElements.openWounds = arrow.attackElements.knockback = false;
            }
            prepareMissileLaunch(arrow, launchCombat, missileCanSlow_ && missileCanSlow_(arrow.missileId),
                missileCanPierce_ && missileCanPierce_(arrow.missileId));
            state_.area.missiles.push_back(std::move(arrow));
            endpoint = endpoint + Vec{float(sideX),float(sideY)};
        }
    } else {
        prepareMissileLaunch(missile, launchCombat, missileCanSlow_ && missileCanSlow_(missile.missileId),
            missileCanPierce_ && missileCanPierce_(missile.missileId));
        state_.area.missiles.push_back(std::move(missile));
    }
    emit(MissileReleased{skill ? skill->missileId : spec.id});
    return true;
}
void Simulation::advancePhysicalMissile(Missile &missile, float dt, std::vector<Missile> &spawned) {
    if (missile.spear && missile.spear->program->poisonTrail && resolveUnitSkill_) {
        const auto skill = resolveUnitSkill_(missile.owner, missile.skillId, missile.skillRank);
        if (skill.weapon && skill.weapon->spear && skill.weapon->spear->poisonTrail) {
            const auto &cloud = *skill.weapon->spear->poisonTrail;
            Missile trail{ids_.allocate(), missile.owner, missile.pos, {}, float(cloud.lifetimeFrames) / 25.f,
                SkillBehavior::None, false, cloud.missileId};
            trail.poisonCloud = cloud; trail.combatRandom = childRandom(unitRandom_);
            trail.skillId = missile.skillId; trail.skillRank = missile.skillRank;
            spawned.push_back(std::move(trail));
        }
    }
    Vec next = missile.pos + missile.velocity * std::min(dt, missile.remaining);
    const bool wall = clipMissilePath(missile.missileId, missile.pos, next);
    const float remaining = std::max(0.f, missile.remaining - dt);
    const bool expired = remaining <= .00001f;
    missile.remaining = remaining;
    while (!expired && missile.remaining > 0) {
        const auto contact = missileTarget(missile, next);
        if (!contact) break;
        const auto struck = combatUnit(contact->first);
        missile.pos = missile.pos + (next - missile.pos) * contact->second;
        const bool continued = consumeMissilePierce(missile, struck.id);
        skills().reactToMissile(missile, struck.id, spawned);
        // Native impact callbacks still run at every pierced contact.
        if (missile.impact) resolveMissileImpact(missile, spawned, struck.id);
        missile.lastHit = struck.id;
        if (missile.hitOverlayId >= 0) state_.area.effects.push_back({*struck.position, 0, missile.hitOverlayDuration, -1, missile.hitOverlayId, struck.id});
        if (missile.nextHitDelay > 0) state_.area.novaHitUntil[struck.id] = state_.time + missile.nextHitDelay;
        const MonsterDefense defense{struck.stats.level, struck.stats.attributes.defense,
            struck.stats.demon, struck.stats.undead, struck.stats.boss};
        if (missile.attackerLevel <= 0) { missile.remaining = 0; return; }
        const bool automatic = (missile.bone && missile.bone->program->spirit) || (missile.spear && missile.spear->program->automaticHit);
        if (!automatic) rollRandom(missile.combatRandom);
        const int chance = missile.weaponAttack ? weaponHitChance(missile.attackerLevel,
            missile.baseAttackRating, missile.attackRatingPercent, missile.targetModifiers, defense, struck.stats.rank) :
            physicalHitChance(missile.attackerLevel, missile.attackRating, defense.level, defense.defense);
        if (!automatic && uint32_t(missile.combatRandom) % 100 >= unsigned(chance)) { missile.remaining = 0; return; }
        const int64_t raw = int64_t(missile.damage * 256.f);
        const int percent = std::max(-90, missile.physicalDamagePercent + targetDamageBonus(missile.targetModifiers, defense));
        const float physical = float(raw + raw * percent / 100) / 256.f;
        resolveWeaponHit(struck.id, physical, missile.owner, missile.attackElements);
        if (!continued) { missile.remaining = 0; return; }
    }
    missile.pos = next;
    if (wall || expired) {
        missile.remaining = 0;
        if (missile.impact) resolveMissileImpact(missile, spawned);
    }
}
} // namespace d2x

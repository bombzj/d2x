#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/projectile_source.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::launchProjectiles(SkillCaster player, const SkillCastSpec &skill, Vec target, EntityId targetUnit) {
    if (skill.heaven) {
        const auto defender = combatUnit(targetUnit);
        if (!defender.alive() || !canAttack(player.id, targetUnit)) return;
        Missile missile{world_.allocate(), player.id, target, {}, float(skill.heaven->delayFrames) / 25.f,
            skill.effect, false, skill.missileId};
        missile.heaven = skill.heaven;
        missile.heavenTarget = targetUnit;
        const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
        missile.damage = float(minimum + limitedRandom(player.combatRandom, unsigned(std::max(0, maximum - minimum)))) / 256.f;
        missile.skillId = skill.sourceId; missile.skillRank = skill.rank;
        missile.hitOverlayId = skill.hitOverlayId; missile.hitOverlayDuration = skill.hitOverlayDuration;
        missile.combatRandom = world_.childSeed();
        world_.addEffect({target, 0, skill.hitOverlayDuration, -1, skill.hitOverlayId, targetUnit});
        world_.addMissile(std::move(missile));
    } else if (skill.arc || skill.meteor) {
        const Vec origin = skill.meteor ? Vec{std::floor(target.x) + .5f, std::floor(target.y) + .5f} :
            Vec{std::floor(player.pos.x) + .5f, std::floor(player.pos.y) + .5f};
        Missile missile{world_.allocate(), player.id, origin,
            skill.arc ? player.look * skill.missileVelocity : Vec{}, skill.missileLifetime,
            skill.effect, false, skill.missileId};
        missile.combatRandom = world_.childSeed();
        missile.skillId = skill.sourceId; missile.skillRank = skill.rank;
        if (skill.arc) {
            missile.arc = Missile::ArcState{*skill.arc, skill.arc->count,
                int(skill.minimumDamage * 256.f), int(skill.maximumDamage * 256.f)};
            missile.hitOverlayId = skill.hitOverlayId; missile.hitOverlayDuration = skill.hitOverlayDuration;
            missile.fixedElement = MonsterDamageType::Lightning;
        } else {
            missile.meteor = skill.meteor;
            const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
            missile.damage = float(minimum + limitedRandom(missile.combatRandom,
                unsigned(std::max(0, maximum - minimum)))) / 256.f;
        }
        world_.addMissile(std::move(missile));
    } else if (skill.blizzard) {
        launchBlizzard({player.id, player.pos, player.look, player.combatRandom}, skill, target);
    } else if (skill.firewall) {
        const auto &definition = *skill.firewall;
        const Vec center{float(int(target.x)), float(int(target.y))};
        const Vec difference{float(int(player.pos.x)) - center.x, float(int(player.pos.y)) - center.y};
        const Vec heading = Vec{-difference.y, difference.x}.unit();
        launchFirewall(player.id, center, heading, definition, skill.effect);
    } else if (skill.frozenOrb) {
        launchFrozenOrb({player.id, player.pos, player.look, player.combatRandom}, skill, target);
    } else if (skill.freezingArea) {
        launchGlacialSpike({player.id, player.pos, player.look, player.combatRandom}, skill, target);
    } else if (skill.effect == SkillBehavior::ChargedBolt) {
        if ((target - player.pos).length() < 1) target = player.pos + player.look * 10;
        for (int index = 0; index < skill.missileCount; ++index) {
            rollRandom(player.combatRandom);
            const int minimum = int(skill.minimumDamage * 256), maximum = int(skill.maximumDamage * 256);
            const float amount = float(minimum + uint32_t(player.combatRandom) % unsigned(maximum - minimum + 1)) / 256.f;
            Missile missile;
            missile.id = world_.allocate();
            missile.combatRandom = world_.childSeed();
            missile.owner = player.id;
            missile.pos = player.pos;
            missile.velocity = player.look * skill.missileVelocity;
            missile.remaining = skill.missileLifetime;
            missile.behavior = skill.effect;
            missile.missileId = skill.missileId;
            missile.damage = amount;
            missile.hitOverlayId = skill.hitOverlayId;
            missile.hitOverlayDuration = skill.hitOverlayDuration;
            const auto path = chargedBoltPath(player.pos, target, index, int(skill.missileLifetime * 25 + .5f));
            missile.path.assign(path.begin(), path.end());
            world_.addMissile(std::move(missile));
        }
    } else if (skill.effect == SkillBehavior::FrostNova || skill.effect == SkillBehavior::Nova) {
        constexpr int directions = 64;
        constexpr int offsets[]{30, 29, 29, 28, 27, 26, 24, 23, 21, 19, 16, 14, 11, 8, 5, 2,
            0, -2, -5, -8, -11, -14, -16, -19, -21, -23, -24, -26, -27, -28, -29, -29,
            -30, -29, -29, -28, -27, -26, -24, -23, -21, -19, -16, -14, -11, -8, -5, -2,
            0, 2, 5, 8, 11, 14, 16, 19, 21, 23, 24, 26, 27, 28, 29, 29};
        for (int index = 0; index < directions; ++index) {
            const Vec heading = Vec{float(offsets[index]), float(offsets[(index + 48) % directions])}.unit();
            rollRandom(player.combatRandom);
            const float fraction = float(uint32_t(player.combatRandom)) / 4294967295.f;
            const float amount = skill.minimumDamage +
                (skill.maximumDamage - skill.minimumDamage) * fraction;
            auto &missile = world_.addMissile({world_.allocate(), player.id, player.pos,
                heading * skill.missileVelocity, skill.missileLifetime, skill.effect,
                false, skill.missileId, amount, 0, skill.coldDuration});
            missile.combatRandom = world_.childSeed();
            missile.nextHitDelay = skill.missileNextDelay;
            missile.acceleration = skill.missileAcceleration;
            missile.maxVelocity = skill.missileMaxVelocity;
            missile.hitOverlayId = skill.hitOverlayId;
            missile.hitOverlayDuration = skill.hitOverlayDuration;
        }
    } else if (skill.effect == SkillBehavior::BlessedHammer) {
        int percent = 100;
        for (const auto &effect : player.combatEffects.entries())
            if (effect.activeAt(world_.frame()) && effect.spec.state.id == skill.concentrationState)
                percent += effect.spec.modifiers.combat.damagePercent * skill.concentrationFactor / 8;
        const int minimum = int(skill.minimumDamage * 256.f) * percent / 100;
        const int maximum = int(skill.maximumDamage * 256.f) * percent / 100;
        Missile missile{world_.allocate(), player.id, player.pos, player.look * skill.missileVelocity,
            skill.missileLifetime, skill.effect, false, skill.missileId};
        missile.damage = float(minimum + limitedRandom(player.combatRandom, unsigned(std::max(0, maximum - minimum)))) / 256.f;
        missile.fixedElement = MonsterDamageType::Magic;
        missile.killOnHit = false;
        missile.combatRandom = world_.childSeed();
        Vec previous{std::floor(player.pos.x), std::floor(player.pos.y)};
        for (int step = 1; missile.path.size() < 77; ++step) {
            const float angle = float(step * 16) * 6.283185307179586f / 512.f;
            const float radius = float(step * 9600) / 65536.f;
            const Vec point{std::floor(player.pos.x + std::cos(angle) * radius),
                            std::floor(player.pos.y + std::sin(angle) * radius)};
            if (point.x == previous.x && point.y == previous.y) continue;
            missile.path.push_back(point + Vec{.5f, .5f});
            previous = point;
        }
        world_.addMissile(std::move(missile));
    } else if (skill.effect == SkillBehavior::FireBolt || skill.effect == SkillBehavior::Fireball ||
               skill.effect == SkillBehavior::IceBolt || skill.effect == SkillBehavior::IceBlast ||
               skill.effect == SkillBehavior::Inferno || skill.effect == SkillBehavior::HolyBolt) {
        rollRandom(player.combatRandom);
        const float fraction = float(uint32_t(player.combatRandom)) / 4294967295.f;
        const float amount = skill.minimumDamage +
                             (skill.maximumDamage - skill.minimumDamage) * fraction;
        auto &missile = launchStraight({{}, player.id, player.pos + player.look * .7f,
            player.look * skill.missileVelocity, skill.missileLifetime, skill.effect,
            false, skill.missileId, amount, 0, skill.coldDuration});
        missile.acceleration = skill.missileAcceleration;
        missile.maxVelocity = skill.missileMaxVelocity;
        missile.impact = skill.missileImpact;
        const bool cold = skill.effect == SkillBehavior::IceBolt || skill.effect == SkillBehavior::IceBlast;
        missile.impactDamage.channels[size_t(cold ? MonsterDamageType::Cold : MonsterDamageType::Fire)] = amount;
        missile.impactDamage.coldDuration = skill.coldDuration;
        missile.impactDamage.freeze = skill.effect == SkillBehavior::IceBlast;
        if (skill.effect == SkillBehavior::HolyBolt) {
            missile.fixedElement = MonsterDamageType::Magic;
            missile.hitOverlayId = skill.hitOverlayId;
            missile.hitOverlayDuration = skill.hitOverlayDuration;
            missile.healingMinimum = skill.healingMinimum;
            missile.healingMaximum = skill.healingMaximum;
            missile.skillId = skill.sourceId;
            missile.skillRank = skill.rank;
        }
    }
}
void SkillRuntime::launchHydraBolt(SkillProjectileSource actor, const SkillCastSpec &skill, Vec target) {
    const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
    Missile missile{world_.allocate(), actor.id, actor.pos, (target - actor.pos).unit() * skill.missileVelocity,
        skill.missileLifetime, SkillBehavior::Hydra, false, skill.missileId,
        float(minimum + limitedRandom(actor.combatRandom, unsigned(std::max(0, maximum - minimum)))) / 256.f};
    missile.combatRandom = world_.childSeed();
    missile.fixedElement = DamageType::Fire;
    missile.impact = skill.missileImpact;
    missile.impactDamage.channels[size_t(DamageType::Fire)] = missile.damage;
    world_.addMissile(std::move(missile));
    emit(MissileReleased{skill.missileId});
}
} // namespace d2x

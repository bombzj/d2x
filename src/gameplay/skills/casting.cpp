#include "core/random.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2x {
void Simulation::stopChannel(PlayerState &player) {
    if (!player.channel) return;
    player.channel.reset();
    player.castTime = 0;
}
void Simulation::advanceSkillCasting(PlayerState &player, float dt, bool moving) {
    auto &channel = player.channel;
    if (channel && (player.dead || player.hitTime > 0 || moving)) stopChannel(player);
    if (channel && channel->enemy) {
        const auto enemy = combatUnit(channel->enemy);
        if (!enemy.alive() || !canAttack(player.id, enemy.id) || !active(*enemy.position)) stopChannel(player);
        else {
            channel->target = *enemy.position;
            const auto direction = (*enemy.position - player.pos).unit();
            if (direction.length() > 0) player.look = direction;
        }
    }
    if (channel) {
        channel->age += dt;
        channel->remaining -= dt;
        while (channel && channel->remaining < -.00001f) {
            const bool consumeMana = channel->pulses % 2 == 0;
            if (consumeMana && player.mana < channel->skill.manaCost) { stopChannel(player); break; }
            if (channel->pulses == 0)
                emit(MissileReleased{channel->skill.missileId});
            releaseSkillCast(player, channel->skill, channel->target, 0, consumeMana);
            ++channel->pulses;
            channel->remaining += 1.f / 25.f;
        }
        if (channel) player.castTime = 1;
    }
    auto &pendingCast = player.pendingCast;
    if (pendingCast && (player.dead || player.hitTime > 0)) {
        pendingCast.reset();
        player.castTime = 0;
    }
    if (pendingCast) {
        pendingCast->remaining -= dt;
        if (pendingCast->remaining <= .00001f) {
            auto cast = *pendingCast;
            pendingCast.reset();
            if (cast.enemy) {
                if (cast.skill.summon) {
                    if (!usableCorpse(cast.enemy)) return;
                    cast.target = unitPosition(cast.enemy);
                } else {
                    const auto enemy = combatUnit(cast.enemy);
                    if (!enemy.alive() || !canAttack(player.id, enemy.id) || !active(*enemy.position)) return;
                    cast.target = *enemy.position;
                    const auto direction = (cast.target - player.pos).unit();
                    if (direction.length() > 0) player.look = direction;
                }
            }
            releaseSkillCast(player, cast.skill, cast.target, cast.staticFieldMinimum, true, cast.enemy);
        }
    }
}
bool Simulation::beginSkillCast(PlayerState &player, const SkillCastSpec &skill, Vec target, bool teleportAllowed,
                              int staticFieldMinimum, EntityId enemy) {
    if (player.channel) {
        if (skill.sourceId == player.channel->skill.sourceId && !player.dead && player.hitTime <= 0) {
            player.channel->target = target;
            player.channel->enemy = enemy;
            const auto direction = (target - player.pos).unit();
            if (direction.length() > 0) player.look = direction;
            return true;
        }
        stopChannel(player);
    }
    if (player.dead || player.castTime > 0 ||
        player.meleeTime > 0 || player.hitTime > 0 || skill.castDuration <= 0)
        return false;
    if (skill.delayFrames > 0 && state_.frame < player.skillDelayUntil) return false;
    if (skill.blizzard && !blizzardTargetClear(player.pos, target)) return false;
    if (skill.effect == SkillBehavior::Teleport && (!teleportAllowed || !grid_->walkable(target, playerMovement))) {
        state_.message = "Teleport needs permitted, clear ground";
        return false;
    }
    if (skill.summon) {
        if (!enemy) enemy = corpseNear(target);
        if (!usableCorpse(enemy)) { state_.message = "A usable monster corpse is required"; return false; }
        target = unitPosition(enemy);
    }
    if (player.mana < std::max(skill.manaCost, skill.startMana)) {
        state_.message = "Not enough mana";
        return false;
    }
    const Vec aim = (target - player.pos).unit();
    if (aim.length() > 0) player.look = aim;
    player.castTime = skill.castDuration;
    player.lastCastDuration = player.castTime;
    player.lastCastRate = skill.castRate;
    player.route.clear();
    player.attackTarget = {};
    player.throwAttack = player.leftHandAttack = false;
    player.attackPosition.reset();
    state_.message.clear();
    if (skill.effect == SkillBehavior::Inferno) {
        player.mana -= skill.manaCost;
        player.channel = PlayerState::ChannelCast{skill, target, skill.castImpact, 0, 0, enemy};
        emit(SkillCast{player.id, skill.sourceId, player.pos});
        return true;
    }
    emit(SkillCast{player.id, skill.sourceId, player.pos});
    if (skill.castOverlayId >= 0)
        state_.area.effects.push_back({player.pos, 0, skill.visualDuration,
                                      -1, skill.castOverlayId, player.id});
    player.pendingCast = PlayerState::PendingCast{skill, target, staticFieldMinimum, skill.castImpact, enemy};
    return true;
}
void Simulation::releaseSkillCast(PlayerState &player, const SkillCastSpec &skill, Vec target,
                                     int staticFieldMinimum, bool consumeMana, EntityId targetUnit) {
    if (player.dead || (consumeMana && player.mana < skill.manaCost) ||
        (skill.effect == SkillBehavior::Teleport && !grid_->walkable(target, playerMovement))) return;
    if (skill.summon) {
        if (summonFromCorpse(player, skill, targetUnit)) {
            if (consumeMana) player.mana -= skill.manaCost;
            emit(SkillActivated{skill.sourceId});
        }
        return;
    }
    if (skill.blizzard && !blizzardTargetClear(player.pos, target)) return;
    if (consumeMana) player.mana -= skill.manaCost;
    if (skill.delayFrames > 0)
        player.skillDelayUntil = state_.frame + EffectFrame(skill.delayFrames);
    if (skill.missileId >= 0 && skill.effect != SkillBehavior::Inferno && !skill.appliedEffect)
        emit(MissileReleased{skill.missileId});
    if (skill.appliedEffect) {
        auto effect = *skill.appliedEffect;
        effect.source.entity = player.id;
        const auto applied = player.combatEffects.apply(std::move(effect), state_.frame);
        combatEffectsChanged(applied.removed);
        emit(SkillActivated{skill.sourceId});
    } else if (skill.effect == SkillBehavior::Teleport) {
        player.pos = player.previous = target;
        relocateCompanions(player.id, target);
        // PetType.hireable.warp=1: SrvDo027 -> SUnit -> PlayerPets relocates
        // living hirelings to the owner's destination, including through walls.
        if (player.hireling.active()) {
            auto &merc = player.hireling;
            merc.pos = target; merc.route.clear(); merc.moving = false;
            merc.attack.reset(); merc.attackTimer = merc.thinkTimer = 0;
            merc.animationTime = 0;
        }
    } else if (skill.effect == SkillBehavior::StaticField) {
        for (auto unit : combatUnits()) {
            if (!unit.alive() || !canAttack(player.id, unit.id) || !active(*unit.position) ||
                (*unit.position - player.pos).length() > skill.staticRadius) continue;
            const int hitpoints = int(*unit.life);
            if (hitpoints < 1 || (staticFieldMinimum > 0 && hitpoints <= unit.stats.attributes.maxLife * staticFieldMinimum / 100)) continue;
            float amount = std::max(skill.staticMinDamage, float(std::min(hitpoints * int(skill.staticPercent) / 100, hitpoints - 1)));
            amount *= float(std::clamp(100 - unitResistance(unit, MonsterDamageType::Lightning), 0, 100)) / 100.f;
            dealDamage({player.id, unit.id, amount, MonsterDamageType::Lightning, 0, true});
        }
    } else if (skill.blizzard) {
        launchBlizzard(player, skill, target);
    } else if (skill.frozenOrb) {
        launchFrozenOrb(player, skill, target);
    } else if (skill.freezingArea) {
        launchGlacialSpike(player, skill, target);
    } else if (skill.effect == SkillBehavior::ChargedBolt) {
        if ((target - player.pos).length() < 1) target = player.pos + player.look * 10;
        for (int index = 0; index < skill.missileCount; ++index) {
            rollRandom(player.combatRandom);
            const int minimum = int(skill.minimumDamage * 256), maximum = int(skill.maximumDamage * 256);
            const float amount = float(minimum + uint32_t(player.combatRandom) % unsigned(maximum - minimum + 1)) / 256.f;
            Missile missile;
            missile.id = ids_.allocate();
            missile.combatRandom = childRandom(unitRandom_);
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
            state_.area.missiles.push_back(std::move(missile));
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
            state_.area.missiles.push_back({ids_.allocate(), player.id, player.pos,
                heading * skill.missileVelocity, skill.missileLifetime, skill.effect,
                false, skill.missileId, amount, 0, skill.coldDuration});
            state_.area.missiles.back().combatRandom = childRandom(unitRandom_);
            state_.area.missiles.back().nextHitDelay = skill.missileNextDelay;
            state_.area.missiles.back().acceleration = skill.missileAcceleration;
            state_.area.missiles.back().maxVelocity = skill.missileMaxVelocity;
            state_.area.missiles.back().hitOverlayId = skill.hitOverlayId;
            state_.area.missiles.back().hitOverlayDuration = skill.hitOverlayDuration;
        }
    } else if (skill.effect == SkillBehavior::FireBolt || skill.effect == SkillBehavior::Fireball ||
               skill.effect == SkillBehavior::IceBolt || skill.effect == SkillBehavior::IceBlast ||
               skill.effect == SkillBehavior::Inferno) {
        rollRandom(player.combatRandom);
        const float fraction = float(uint32_t(player.combatRandom)) / 4294967295.f;
        const float amount = skill.minimumDamage +
                             (skill.maximumDamage - skill.minimumDamage) * fraction;
        state_.area.missiles.push_back({ids_.allocate(), player.id, player.pos + player.look * .7f,
            player.look * skill.missileVelocity, skill.missileLifetime, skill.effect,
            false, skill.missileId, amount, 0, skill.coldDuration});
        state_.area.missiles.back().acceleration = skill.missileAcceleration;
        state_.area.missiles.back().maxVelocity = skill.missileMaxVelocity;
        auto &missile = state_.area.missiles.back();
        missile.impact = skill.missileImpact;
        const bool cold = skill.effect == SkillBehavior::IceBolt || skill.effect == SkillBehavior::IceBlast;
        missile.impactDamage.channels[size_t(cold ? MonsterDamageType::Cold : MonsterDamageType::Fire)] = amount;
        missile.impactDamage.coldDuration = skill.coldDuration;
        missile.impactDamage.freeze = skill.effect == SkillBehavior::IceBlast;
        missile.combatRandom = childRandom(unitRandom_);
    }
}
} // namespace d2x

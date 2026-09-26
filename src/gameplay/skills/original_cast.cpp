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
void Simulation::advanceOriginalCasting(PlayerState &player, float dt, bool moving) {
    auto &channel = player.channel;
    if (channel && (player.dead || player.hitTime > 0 || moving)) stopChannel(player);
    if (channel) {
        channel->age += dt;
        channel->remaining -= dt;
        while (channel && channel->remaining < -.00001f) {
            const bool consumeMana = channel->pulses % 2 == 0;
            if (consumeMana && player.mana < channel->skill.manaCost) { stopChannel(player); break; }
            if (channel->pulses == 0)
                emit(MissileReleased{channel->skill.missileId});
            releaseOriginalCast(player, channel->skill, channel->target, 0, consumeMana);
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
            const auto cast = *pendingCast;
            pendingCast.reset();
            releaseOriginalCast(player, cast.skill, cast.target, cast.staticFieldMinimum);
        }
    }
}
bool Simulation::castOriginal(PlayerState &player, const OriginalSkillCast &skill, Vec target, bool teleportAllowed,
                              int staticFieldMinimum) {
    if (player.channel) {
        if (skill.effect == Skill::Inferno && !player.dead && player.hitTime <= 0) {
            player.channel->target = target;
            const auto direction = (target - player.pos).unit();
            if (direction.length() > 0) player.look = direction;
            return true;
        }
        stopChannel(player);
    }
    if (player.dead || player.castTime > 0 || player.spinTime > 0 || player.leapTime > 0 ||
        player.meleeTime > 0 || player.hitTime > 0 || skill.castDuration <= 0)
        return false;
    if (skill.effect == Skill::Teleport && (!teleportAllowed || !grid_->walkable(target))) {
        state_.message = "Teleport needs permitted, clear ground";
        return false;
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
    player.lastSkill = skill.effect;
    player.route.clear();
    player.attackTarget = {};
    player.throwAttack = player.leftHandAttack = false;
    state_.message.clear();
    if (skill.effect == Skill::Inferno) {
        player.mana -= skill.manaCost;
        player.channel = PlayerState::ChannelCast{skill, target, skill.castImpact};
        emit(SkillCast{player.id, skill.effect, player.pos});
        return true;
    }
    emit(SkillCast{player.id, skill.effect, player.pos});
    if (skill.castOverlayId >= 0)
        state_.area.effects.push_back({player.pos, skill.effect, 0, skill.visualDuration,
                                      -1, skill.castOverlayId, player.id});
    player.pendingCast = PlayerState::PendingCast{skill, target, staticFieldMinimum, skill.castImpact};
    return true;
}
void Simulation::releaseOriginalCast(PlayerState &player, const OriginalSkillCast &skill, Vec target,
                                     int staticFieldMinimum, bool consumeMana) {
    if (player.dead || (consumeMana && player.mana < skill.manaCost) ||
        (skill.effect == Skill::Teleport && !grid_->walkable(target))) return;
    if (consumeMana) player.mana -= skill.manaCost;
    if (skill.missileId >= 0 && skill.effect != Skill::Inferno) emit(MissileReleased{skill.missileId});
    if (skill.effect == Skill::FrozenArmor) {
        ActiveCombatEffect effect;
        effect.owner = player.id;
        effect.sourceId = skill.sourceId;
        effect.group = skill.stateGroup;
        effect.startedAt = state_.time;
        effect.expiresAt = state_.time + skill.buffDuration;
        effect.modifiers.combat.defensePercent = skill.defensePercent;
        effect.retaliationFreeze = skill.retaliationFreeze;
        effect.overlayId = skill.stateOverlayId;
        effect.hitOverlayId = skill.hitOverlayId;
        effect.hitOverlayDuration = skill.hitOverlayDuration;
        if (applyCombatEffect_) applyCombatEffect_(std::move(effect));
        emit(SkillActivated{skill.effect});
    } else if (skill.effect == Skill::Teleport) {
        player.pos = player.previous = target;
    } else if (skill.effect == Skill::StaticField) {
        for (auto &enemy : state_.area.enemies) {
            if (enemy.hp <= 0 || !active(enemy.pos) ||
                (enemy.pos - player.pos).length() > skill.staticRadius) continue;
            const int hitpoints = int(enemy.hp);
            if (hitpoints < 1 || (staticFieldMinimum > 0 &&
                hitpoints <= int(enemy.maxHp) * staticFieldMinimum / 100)) continue;
            float amount = std::max(skill.staticMinDamage,
                float(std::min(hitpoints * int(skill.staticPercent) / 100, hitpoints - 1)));
            if (monsterResistance_)
                if (auto resistance = monsterResistance_(enemy, state_.area.region, MonsterDamageType::Lightning))
                    amount *= float(std::clamp(100 - *resistance, 0, 100)) / 100.f;
            if (amount > 0) damageEnemy(enemy, amount, player.id, 0, false,
                                        MonsterDamageType::Lightning, true);
        }
    } else if (skill.effect == Skill::ChargedBolt) {
        if ((target - player.pos).length() < 1) target = player.pos + player.look * 10;
        for (int index = 0; index < skill.missileCount; ++index) {
            player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                  (player.combatRandom >> 32);
            const int minimum = int(skill.minimumDamage * 256), maximum = int(skill.maximumDamage * 256);
            const float amount = float(minimum + uint32_t(player.combatRandom) % unsigned(maximum - minimum + 1)) / 256.f;
            Missile missile;
            missile.id = ids_.allocate();
            missile.owner = player.id;
            missile.pos = player.pos;
            missile.velocity = player.look * skill.missileVelocity;
            missile.remaining = skill.missileLifetime;
            missile.skill = skill.effect;
            missile.missileId = skill.missileId;
            missile.damage = amount;
            missile.hitOverlayId = skill.hitOverlayId;
            missile.hitOverlayDuration = skill.hitOverlayDuration;
            const auto path = chargedBoltPath(player.pos, target, index, int(skill.missileLifetime * 25 + .5f));
            missile.path.assign(path.begin(), path.end());
            state_.area.missiles.push_back(std::move(missile));
        }
    } else if (skill.effect == Skill::FrostNova || skill.effect == Skill::Nova) {
        constexpr int directions = 64;
        constexpr int offsets[]{30, 29, 29, 28, 27, 26, 24, 23, 21, 19, 16, 14, 11, 8, 5, 2,
            0, -2, -5, -8, -11, -14, -16, -19, -21, -23, -24, -26, -27, -28, -29, -29,
            -30, -29, -29, -28, -27, -26, -24, -23, -21, -19, -16, -14, -11, -8, -5, -2,
            0, 2, 5, 8, 11, 14, 16, 19, 21, 23, 24, 26, 27, 28, 29, 29};
        for (int index = 0; index < directions; ++index) {
            const Vec heading = Vec{float(offsets[index]), float(offsets[(index + 48) % directions])}.unit();
            player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                  (player.combatRandom >> 32);
            const float fraction = float(uint32_t(player.combatRandom)) / 4294967295.f;
            const float amount = skill.minimumDamage +
                (skill.maximumDamage - skill.minimumDamage) * fraction;
            state_.area.missiles.push_back({ids_.allocate(), player.id, player.pos,
                heading * skill.missileVelocity, skill.missileLifetime, skill.effect,
                false, skill.missileId, amount, 0, skill.coldDuration});
            state_.area.missiles.back().nextHitDelay = skill.missileNextDelay;
            state_.area.missiles.back().acceleration = skill.missileAcceleration;
            state_.area.missiles.back().maxVelocity = skill.missileMaxVelocity;
            state_.area.missiles.back().hitOverlayId = skill.hitOverlayId;
            state_.area.missiles.back().hitOverlayDuration = skill.hitOverlayDuration;
        }
    } else {
        player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                              (player.combatRandom >> 32);
        const float fraction = float(uint32_t(player.combatRandom)) / 4294967295.f;
        const float amount = skill.minimumDamage +
                             (skill.maximumDamage - skill.minimumDamage) * fraction;
        state_.area.missiles.push_back({ids_.allocate(), player.id, player.pos + player.look * .7f,
            player.look * skill.missileVelocity, skill.missileLifetime, skill.effect,
            false, skill.missileId, amount, skill.impactRadius, skill.coldDuration});
        state_.area.missiles.back().acceleration = skill.missileAcceleration;
        state_.area.missiles.back().maxVelocity = skill.missileMaxVelocity;
        state_.area.missiles.back().impactMissileId = skill.impactMissileId;
        state_.area.missiles.back().impactDuration = skill.impactDuration;
    }
}
} // namespace d2x

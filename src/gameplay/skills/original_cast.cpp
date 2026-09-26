#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2x {
bool Simulation::castOriginal(const OriginalSkillCast &skill, Vec target, bool teleportAllowed,
                              int staticFieldMinimum) {
    auto &player = state_.player;
    if (player.dead || player.castTime > 0 || player.spinTime > 0 || player.leapTime > 0 ||
        player.meleeTime > 0 || player.hitTime > 0 || skill.castDuration <= 0)
        return false;
    if (skill.effect == Skill::Teleport && (!teleportAllowed || !grid_->walkable(target))) {
        state_.message = "Teleport needs permitted, clear ground";
        return false;
    }
    if (player.mana < skill.manaCost) {
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
    emit(SkillCast{player.id, skill.effect, player.pos});
    if (skill.castOverlayId >= 0)
        state_.area.effects.push_back({player.pos, skill.effect, 0, skill.visualDuration,
                                      -1, skill.castOverlayId, player.id});
    pendingCast_ = PendingCast{skill, target, staticFieldMinimum, skill.castImpact};
    return true;
}
void Simulation::releaseOriginalCast(const OriginalSkillCast &skill, Vec target, int staticFieldMinimum) {
    auto &player = state_.player;
    if (player.dead || player.mana < skill.manaCost ||
        (skill.effect == Skill::Teleport && !grid_->walkable(target))) return;
    player.mana -= skill.manaCost;
    if (skill.missileId >= 0) emit(MissileReleased{skill.missileId});
    if (skill.effect == Skill::Teleport) {
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

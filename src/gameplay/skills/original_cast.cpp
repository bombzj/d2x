#include "gameplay/simulation/simulation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2x {
bool Simulation::castOriginal(const OriginalSkillCast &skill, Vec target, bool teleportAllowed,
                              int staticFieldMinimum) {
    auto &player = state_.player;
    if (player.dead || player.castTime > 0 || player.spinTime > 0 || player.leapTime > 0)
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
    player.mana -= skill.manaCost;
    player.castTime = skillDefinition(skill.effect).castDuration;
    player.lastCastDuration = player.castTime;
    player.lastSkill = skill.effect;
    player.route.clear();
    player.attackTarget = {};
    player.throwAttack = player.leftHandAttack = false;
    state_.message.clear();
    emit(SkillCast{player.id, skill.effect, player.pos});
    if (skill.effect == Skill::Teleport) {
        state_.area.effects.push_back({player.pos, skill.effect, 0, skill.visualDuration});
        player.pos = player.previous = target;
        state_.area.effects.push_back({player.pos, skill.effect, 0, skill.visualDuration});
    } else if (skill.effect == Skill::StaticField) {
        for (auto &enemy : state_.area.enemies) {
            if (enemy.hp <= 0 || !active(enemy.pos) ||
                (enemy.pos - player.pos).length() > skill.staticRadius) continue;
            const float floor = std::max(1.f,
                enemy.maxHp * float(staticFieldMinimum) / 100.f);
            const float amount = std::min(enemy.hp * skill.staticPercent / 100.f, enemy.hp - floor);
            if (amount > 0) damageEnemy(enemy, amount, player.id, 0, false,
                                        MonsterDamageType::Lightning);
        }
    } else if (skill.effect == Skill::FrostNova) {
        state_.area.effects.push_back({player.pos, skill.effect, 0, skill.missileLifetime});
        const float radius = skill.missileVelocity * skill.missileLifetime;
        for (auto &enemy : state_.area.enemies) {
            if (enemy.hp <= 0 || !active(enemy.pos) ||
                (enemy.pos - player.pos).length() > radius || !grid_->segment(player.pos, enemy.pos))
                continue;
            player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                  (player.combatRandom >> 32);
            const float fraction = float(uint32_t(player.combatRandom)) / 4294967295.f;
            const float amount = skill.minimumDamage +
                (skill.maximumDamage - skill.minimumDamage) * fraction;
            damageEnemy(enemy, amount, player.id, skill.coldDuration, false,
                        MonsterDamageType::Cold);
        }
    } else {
        player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                              (player.combatRandom >> 32);
        const float fraction = float(uint32_t(player.combatRandom)) / 4294967295.f;
        const float amount = skill.minimumDamage +
                             (skill.maximumDamage - skill.minimumDamage) * fraction;
        state_.area.missiles.push_back({ids_.allocate(), player.id, player.pos + player.look * .7f,
            player.look * skill.missileVelocity, skill.missileLifetime, skill.effect,
            false, skill.missileId, amount, skill.impactRadius, 0});
    }
    return true;
}
} // namespace d2x

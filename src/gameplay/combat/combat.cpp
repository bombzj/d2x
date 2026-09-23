#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include <algorithm>

namespace d2x {
void Simulation::damageEnemy(Enemy &enemy, float amount, EntityId source, float chill,
                             bool ignoreActivation) {
    if (enemy.hp <= 0 || (!ignoreActivation && !active(enemy.pos)))
        return;
    enemy.hp = std::max(0.f, enemy.hp - amount);
    enemy.hitFlash = .12f;
    enemy.chill = std::max(enemy.chill, chill);
    if (enemy.hp == 0) {
        enemy.deathAge = 0;
        enemy.route.clear();
        enemy.attack = enemy.attackDuration = 0;
        enemy.attackImpact = -1;
        enemy.attackMode = 1;
        ++state_.area.kills;
        emit(EnemyDied{enemy.id, source, enemy.kind, state_.area.region, enemy.pos, enemy.identity,
                       state_.population.difficulty});
    }
}
void Simulation::meleeDamage(Enemy &enemy, bool leftHand) {
    auto &player = state_.player;
    const WeaponDamage *weapon = &equipmentStats_.weapons[player.nextWeapon % equipmentStats_.weaponCount];
    if (leftHand) {
        for (int index = 0; index < equipmentStats_.weaponCount; ++index)
            if (equipmentStats_.weapons[index].leftHand) weapon = &equipmentStats_.weapons[index];
    } else
        player.nextWeapon = (player.nextWeapon + 1) % equipmentStats_.weaponCount;
    player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                          (player.combatRandom >> 32);
    if (monsterDefense_)
        if (auto defense = monsterDefense_(enemy)) {
            if (uint32_t(player.combatRandom) % 100 >=
                unsigned(physicalHitChance(player.level, characterStats_.attackRating,
                                           defense->level, defense->defense)))
                return;
            player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                  (player.combatRandom >> 32);
        }
    auto range = uint32_t(weapon->maximum - weapon->minimum);
    auto damage = weapon->minimum + (range ? uint32_t(player.combatRandom) % range : 0);
    damageEnemy(enemy, float(damage) / 256.f, player.id);
    if (wearEquipment_ && weapon->item)
        wearEquipment_(weapon->item, false);
}
void Simulation::damage(Vec pos, float radius, float amount, EntityId source, float chill) {
    for (auto &e : state_.area.enemies) {
        if (e.hp <= 0 || !active(e.pos) || (e.pos - pos).length() > radius)
            continue;
        damageEnemy(e, amount, source, chill);
    }
}
void Simulation::updateMissiles(float dt) {
    auto &area = state_.area;
    for (auto &m : area.missiles) {
        auto next = m.pos + m.velocity * dt;
        if (!m.physical && m.missileId >= 0) {
            const bool wall = !grid_->segment(m.pos, next);
            if (wall) {
                float clear = 0.f, blocked = 1.f;
                for (int step = 0; step < 9; ++step) {
                    const float middle = (clear + blocked) * .5f;
                    if (grid_->segment(m.pos, m.pos + (next - m.pos) * middle)) clear = middle;
                    else blocked = middle;
                }
                next = m.pos + (next - m.pos) * clear;
            }
            Enemy *struck = nullptr;
            float first = 2.f;
            const Vec motion = next - m.pos;
            const float lengthSquared = motion.x * motion.x + motion.y * motion.y;
            for (auto &enemy : area.enemies) {
                if (enemy.hp <= 0 || !active(enemy.pos)) continue;
                const Vec offset = enemy.pos - m.pos;
                const float projection = lengthSquared > 0 ?
                    std::clamp((offset.x * motion.x + offset.y * motion.y) / lengthSquared, 0.f, 1.f) : 0.f;
                const Vec closest = m.pos + motion * projection;
                if ((enemy.pos - closest).length() < 1.2f && projection < first) {
                    struck = &enemy;
                    first = projection;
                }
            }
            m.pos = struck ? m.pos + motion * first : next;
            m.remaining -= dt;
            if (wall || struck || m.remaining <= 0) {
                m.remaining = 0;
                if (m.radius > 0) {
                    damage(m.pos, m.radius, m.damage, m.owner, m.chill);
                    area.effects.push_back({m.pos, m.skill, 0, .55f});
                } else if (struck)
                    damageEnemy(*struck, m.damage, m.owner, m.chill);
            }
            continue;
        }
        if (m.physical) {
            if (!grid_->segment(m.pos, next)) {
                m.remaining = 0;
                continue;
            }
            Enemy *struck = nullptr;
            float first = 2.f;
            const Vec motion = next - m.pos;
            const float lengthSquared = motion.x * motion.x + motion.y * motion.y;
            for (auto &enemy : area.enemies) {
                if (enemy.hp <= 0 || !active(enemy.pos)) continue;
                const Vec offset = enemy.pos - m.pos;
                const float projection = lengthSquared > 0 ?
                    std::clamp((offset.x * motion.x + offset.y * motion.y) / lengthSquared, 0.f, 1.f) : 0.f;
                const Vec closest = m.pos + motion * projection;
                if ((enemy.pos - closest).length() < 1.2f && projection < first) {
                    struck = &enemy;
                    first = projection;
                }
            }
            m.pos = next;
            m.remaining -= dt;
            if (struck) {
                m.remaining = 0;
                auto &player = state_.player;
                player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                      (player.combatRandom >> 32);
                bool hit = true;
                if (monsterDefense_)
                    if (auto defense = monsterDefense_(*struck))
                        hit = uint32_t(player.combatRandom) % 100 <
                            unsigned(physicalHitChance(player.level, characterStats_.attackRating,
                                                       defense->level, defense->defense));
                if (hit) damageEnemy(*struck, m.damage, m.owner);
            }
            continue;
        }
        bool hit = !grid_->segment(m.pos, next);
        for (const auto &e : area.enemies)
            if (e.hp > 0 && active(e.pos) && (e.pos - next).length() < 1.3f)
                hit = true;
        m.pos = next;
        m.remaining -= dt;
        if (hit) {
            m.remaining = 0;
            const auto &skill = skillDefinition(m.skill);
            damage(m.pos, skill.radius, skill.damage, m.owner);
            area.effects.push_back({m.pos, m.skill, 0, .55f});
        }
    }
    std::erase_if(area.missiles, [](const Missile &m) { return m.remaining <= 0; });
}
} // namespace d2x

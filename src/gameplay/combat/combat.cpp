#include "gameplay/simulation/simulation.hpp"
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
        ++state_.area.kills;
        emit(EnemyDied{enemy.id, source, enemy.kind, state_.area.region, enemy.pos, enemy.identity,
                       state_.population.difficulty});
    }
}
void Simulation::meleeDamage(Enemy &enemy) {
    auto &player = state_.player;
    const auto &weapon = equipmentStats_.weapons[player.nextWeapon % equipmentStats_.weaponCount];
    player.nextWeapon = (player.nextWeapon + 1) % equipmentStats_.weaponCount;
    player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                          (player.combatRandom >> 32);
    auto range = uint32_t(weapon.maximum - weapon.minimum);
    auto damage = weapon.minimum + (range ? uint32_t(player.combatRandom) % range : 0);
    damageEnemy(enemy, float(damage) / 256.f, player.id);
    if (wearEquipment_ && weapon.item)
        wearEquipment_(weapon.item, false);
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

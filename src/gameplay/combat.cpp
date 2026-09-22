#include "simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::damage(Vec pos, float radius, float amount, EntityId source, float chill) {
    for (auto &e : state_.area.enemies) {
        if (e.hp <= 0 || (e.pos - pos).length() > radius)
            continue;
        e.hp = std::max(0.f, e.hp - amount);
        e.hitFlash = .12f;
        e.chill = std::max(e.chill, chill);
        if (e.hp == 0) {
            e.deathAge = 0;
            e.route.clear();
            ++state_.area.kills;
            emit(EnemyDied{e.id, source, e.kind, state_.area.region, e.pos, e.identity,
                           state_.population.difficulty});
        }
    }
}
void Simulation::updateMissiles(float dt) {
    auto &area = state_.area;
    for (auto &m : area.missiles) {
        auto next = m.pos + m.velocity * dt;
        bool hit = !grid_->segment(m.pos, next);
        for (const auto &e : area.enemies)
            if (e.hp > 0 && (e.pos - next).length() < 1.3f)
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

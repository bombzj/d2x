#include "simulation.hpp"
#include <algorithm>

namespace d2x {
void Simulation::updateMonsters(float dt) {
    auto &p = state_.player;
    for (auto &e : state_.area.enemies) {
        if (e.hp <= 0 || !active(e.pos))
            continue;
        e.chill = std::max(0.f, e.chill - dt);
        e.stun = std::max(0.f, e.stun - dt);
        e.attack -= dt;
        if (e.stun > 0)
            continue;
        const auto &def = monsterDefinition(e.kind);
        auto delta = p.pos - e.pos;
        float distance = delta.length();
        if (distance > 1.4f && distance < def.sightRange) {
            e.rethink -= dt;
            Vec heading = delta.unit();
            if (!grid_->segment(e.pos, p.pos)) {
                if (e.rethink <= 0) {
                    e.route = grid_->path(e.pos, p.pos);
                    e.rethink = .7f;
                }
                if (!e.route.empty()) {
                    if ((e.route.front() - e.pos).length() < .25f)
                        e.route.pop_front();
                    if (!e.route.empty())
                        heading = (e.route.front() - e.pos).unit();
                }
            }
            float speed = def.speed * (e.chill > 0 ? .42f : 1.f);
            auto next = e.pos + heading * dt * speed;
            if (grid_->segment(e.pos, next))
                e.pos = next;
        }
        if (distance < def.attackRange && e.attack <= 0 && p.leapTime <= 0) {
            p.hp = std::max(0.f, p.hp - def.damage);
            p.hitTime = .16f;
            e.attack = def.attackInterval * (e.chill > 0 ? 2.f : 1.f);
        }
    }
}
} // namespace d2x

#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void SkillRuntime::advanceHeaven(Missile &m, float dt, std::vector<Missile> &spawned) {

    m.remaining = std::max(0.f, m.remaining - dt);
    if (m.remaining > .00001f) return;
    m.remaining = 0;
    const auto target = combatUnit(m.heavenTarget);
    if (!target.alive()) return;
    dealDamage({m.owner, target.id, m.damage, MonsterDamageType::Lightning});
    int count = 0;
    for (auto candidate : combatUnits()) {
        if (!candidate.alive() || !canAttack(m.owner, candidate.id) || !active(*candidate.position)) continue;
        const int deltaX = int(candidate.position->x) - int(m.pos.x), deltaY = int(candidate.position->y) - int(m.pos.y);
        if (deltaX * deltaX + deltaY * deltaY > m.heaven->radius * m.heaven->radius) continue;
        if (count++ >= m.heaven->limit) break;
        Vec heading = (*candidate.position - m.pos).unit();
        if (heading.length() < .001f) {
            const auto owner = combatUnit(m.owner);
            heading = owner ? (m.pos - *owner.position).unit() : Vec{1, 0};
            if (heading.length() < .001f) heading = {1, 0};
        }
        Missile bolt{world_.allocate(), m.owner, m.pos, heading * (float(m.heaven->boltVelocity * 256 * 75 / 100) * 25.f / 4096.f),
            float(m.heaven->boltFrames) / 25.f, SkillBehavior::HolyBolt, false, m.heaven->boltId};
        bolt.damage = float(m.heaven->minimum + limitedRandom(m.combatRandom,
            unsigned(std::max(0, m.heaven->maximum - m.heaven->minimum)))) / 256.f;
        bolt.fixedElement = MonsterDamageType::Magic;
        bolt.healingMinimum = float(m.heaven->healingMinimum); bolt.healingMaximum = float(m.heaven->healingMaximum);
        bolt.combatRandom = world_.childSeed();
        spawned.push_back(std::move(bolt));
    }
}
} // namespace d2x

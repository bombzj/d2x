#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
bool SkillRuntime::advanceHolyBolt(Missile &m, Vec next, int size, std::vector<Missile> &spawned) {
    if (m.behavior != SkillBehavior::HolyBolt) return false;

    std::optional<std::pair<EntityId, float>> contact;
    for (auto target : combatUnits()) {
        if (!target.alive() || target.id == m.owner || !active(*target.position)) continue;
        const bool ally = relation(m.owner, target.id) == Relation::Allied;
        if (!ally && (!canAttack(m.owner, target.id) || !target.stats.undead)) continue;
        const auto intersection = missileUnitIntersection(m.pos, next, size,
            *target.position, target.stats.collisionSize);
        if (intersection && (!contact || *intersection < contact->second)) contact = {target.id, *intersection};
    }
    m.pos = contact ? m.pos + (next - m.pos) * contact->second : next;
    if (contact) {
        auto target = combatUnit(contact->first);
        if (relation(m.owner, target.id) == Relation::Allied) {
            reactToMissile(m, target.id, spawned);
            const int minimum = int(m.healingMinimum * 256.f), maximum = int(m.healingMaximum * 256.f);
            const float amount = float(minimum + limitedRandom(m.combatRandom,
                unsigned(std::max(0, maximum - minimum)))) / 256.f;
            *target.life = std::min(float(target.stats.attributes.maxLife), *target.life + amount);
            if (m.hitOverlayId >= 0)
                world_.addEffect({*target.position, 0, m.hitOverlayDuration, -1, m.hitOverlayId, target.id});
        } else {
            const int healingOverlay = m.hitOverlayId;
            m.hitOverlayId = -1;
            hitMissile(m, target.id, spawned);
            m.hitOverlayId = healingOverlay;
        }
        m.remaining = 0;
    }

    return true;
}
} // namespace d2x

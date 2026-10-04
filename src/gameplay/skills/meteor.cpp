#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::advanceMeteor(Missile &missile, std::vector<Missile> &spawned) {
    missile.age += 1.f / 25.f;
    missile.remaining = std::max(0.f, missile.remaining - 1.f / 25.f);
    if (missile.remaining > .00001f) return;
    missile.remaining = 0;
    if (!combatUnit(missile.owner)) return;
    const auto &program = *missile.meteor;
    for (const auto &target : combatUnits()) {
        if (!target.alive() || !canAttack(missile.owner, target.id) || !active(*target.position)) continue;
        const int deltaX = int(target.position->x) - int(missile.pos.x);
        const int deltaY = int(target.position->y) - int(missile.pos.y);
        if (deltaX * deltaX + deltaY * deltaY <= program.radius * program.radius && !world_.avoidMissile(target.id))
            dealDamage({missile.owner, target.id, missile.damage, MonsterDamageType::Fire});
    }
    emit(MissileImpact{missile.missileId, missile.pos});
    constexpr Vec offsets[]{{2,-2},{-2,-2},{0,2},{0,5},{-3,3},{0,3},{3,3},{-1,2},{1,1},
        {-1,-1},{2,-1},{-4,-2},{-3,-2},{-1,-3},{0,-4},{1,-3},{3,-3},{4,-2}};
    auto fire = program.fire;
    if (world_.hasResolver()) fire = world_.resolve(missile.owner, missile.skillId, missile.skillRank).meteor->fire;
    for (int index = 0; index < 18; index += program.fireStep) {
        const Vec position = missile.pos + offsets[index];
        if (!world_.missileSegment(position, position, {5, 1})) continue;
        Missile child{world_.allocate(), missile.owner, position, {}, float(fire.fireFrames) / 25.f,
            SkillBehavior::Meteor, false, fire.fireId};
        child.combatRandom = world_.childSeed();
        child.firewall = Missile::FirewallState{fire, false, 0};
        spawned.push_back(std::move(child));
    }
}
} // namespace d2x

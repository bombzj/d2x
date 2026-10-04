#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
void SkillRuntime::reactToMissile(const Missile &incoming, EntityId target, std::vector<Missile> &spawned) {
    if (!world_.returnFire(incoming.missileId)) return;
    const auto unit = combatUnit(target), owner = combatUnit(incoming.owner);
    if (!unit.alive() || !owner || !canAttack(target, incoming.owner)) return;
    for (const auto &trigger : unit.effects->reactions(CombatEffectEvent::HitByMissile, world_.frame())) {
        if (!std::holds_alternative<ColdMissileRetaliation>(trigger.action)) continue;
        if (!world_.hasResolver()) throw std::runtime_error("Chilling Armor owner has no skill resolver");
        const auto skill = world_.resolve(target, trigger.source.definition, trigger.source.level);
        if (skill.effect != SkillBehavior::ChillingArmor || skill.missileId < 0)
            throw std::runtime_error("Missing Chilling Armor retaliation missile");
        // EventFunc01 targets the hostile missile OWNER's current integer position.
        // A straight shot is created once; it does not home, consume mana or
        // restart the armor's SC animation. ReturnFire=0 prevents reflection loops.
        const Vec origin{std::floor(unit.position->x) + .5f, std::floor(unit.position->y) + .5f};
        Vec heading{std::floor(owner.position->x) - std::floor(origin.x),
                    std::floor(owner.position->y) - std::floor(origin.y)};
        if (heading.length() == 0) heading = {1, 1};
        Missile bolt{world_.allocate(), target, origin, heading.unit() * skill.missileVelocity,
            skill.missileLifetime, skill.effect, false, skill.missileId};
        bolt.combatRandom = world_.childSeed();
        bolt.skillId = skill.sourceId; bolt.skillRank = skill.rank;
        bolt.fixedElement = MonsterDamageType::Cold;
        bolt.coldRetaliation.emplace();
        auto &state = *bolt.coldRetaliation;
        state.lifetimeFrames = int(skill.missileLifetime * 25.f + .5f);
        state.minimumDamage = int(skill.minimumDamage * 256.f);
        state.maximumDamage = int(skill.maximumDamage * 256.f);
        state.coldFrames = int(skill.coldDuration * 25.f + .5f);
        // Queue children until the current missile pass completes: creation
        // cannot invalidate a live parent reference or step children immediately.
        spawned.push_back(std::move(bolt));
        emit(MissileReleased{skill.missileId});
        // Native client state event 1 is incomplete in the local reference;
        // display its linked original flash on the reacting armor owner.
        if (skill.hitOverlayId >= 0)
            world_.addEffect({*unit.position, 0, skill.hitOverlayDuration, -1, skill.hitOverlayId, target});
    }
}
void SkillRuntime::advanceChillingArmorBolt(Missile &missile, std::vector<Missile> &spawned) {
    auto &state = *missile.coldRetaliation;
    if (state.elapsedFrames >= state.lifetimeFrames) { missile.remaining = 0; return; }
    Vec next = missile.pos + missile.velocity * (1.f / 25.f);
    const bool wall = world_.clipPath(missile.missileId, missile.pos, next);
    ++state.elapsedFrames;
    missile.age = float(state.elapsedFrames) / 25.f;
    missile.remaining = float(state.lifetimeFrames - state.elapsedFrames) / 25.f;
    // SrvDo1 expires before unit search. No hit function, area explosion or
    // AlwaysExplode exists on this row, so terrain/expiry do not deal damage.
    if (state.elapsedFrames == state.lifetimeFrames) { missile.pos = next; return; }
    const auto contact = world_.missileTarget(missile, next);
    missile.pos = contact ? missile.pos + (next - missile.pos) * contact->second : next;
    if (contact) {
        reactToMissile(missile, contact->first, spawned);
        const auto target = combatUnit(contact->first);
        const int minimum = std::min(state.minimumDamage, state.maximumDamage);
        const uint32_t span = uint32_t(std::abs(state.maximumDamage - state.minimumDamage));
        const float damage = float(minimum + limitedRandom(missile.combatRandom, span)) / 256.f;
        dealDamage({missile.owner, target.id, damage, MonsterDamageType::Cold,
                    missileColdDuration(missile.owner, target, state.coldFrames)});
        emit(MissileImpact{missile.missileId, missile.pos});
        missile.remaining = 0;
    } else if (wall) missile.remaining = 0;
}
} // namespace d2x

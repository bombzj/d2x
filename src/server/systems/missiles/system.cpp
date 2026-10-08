#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::missiles {
DomainResult<EntityId> System::spawn(const Spawn &request) {
    const auto &actor = request.actor;
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (!area || area->generation != actor.areaGeneration || area->definition.town) return {DomainStatus::Unavailable, {}};
    const auto &skill = request.skill;
    if ((skill.effect != SkillBehavior::FireBolt && skill.effect != SkillBehavior::Fireball) || skill.missileId < 0 ||
        !std::isfinite(skill.missileVelocity) || skill.missileVelocity <= 0 || !std::isfinite(skill.missileLifetime) ||
        !std::isfinite(skill.missileAcceleration) || !std::isfinite(skill.missileMaxVelocity) || skill.missileMaxVelocity < 0 ||
        (skill.missileAcceleration != 0 && skill.missileMaxVelocity <= 0) ||
        (skill.missileImpact && (!std::isfinite(skill.missileImpact->radius) || skill.missileImpact->radius < 0)) ||
        skill.missileLifetime <= 0 || skill.missileLifetime * 25 > UINT16_MAX || !std::isfinite(skill.minimumDamage) ||
        !std::isfinite(skill.maximumDamage) || skill.minimumDamage < 0 || skill.maximumDamage < skill.minimumDamage ||
        double(skill.maximumDamage) * 256 > INT32_MAX || !std::isfinite(request.target.x) || !std::isfinite(request.target.y) ||
        (skill.missileImpact && (skill.missileImpact->cloudBurst || skill.missileImpact->areaMissile))) return {DomainStatus::InvalidRequest, {}};
    if (state_.missiles.size() >= 4096 || ports_.ids.cursor() >= UINT32_MAX) return {DomainStatus::Capacity, {}};
    auto heading = (request.target - player->position).unit();
    if (!heading.length()) heading = player->look;
    if (!heading.length()) return {DomainStatus::Unavailable, {}};
    auto random = ports_.random;
    // Old launchProjectiles draw: preserve its damage interpolation, then store fixed HP units.
    const float fraction = float(rollRandom(random)) / 4294967295.f;
    Missile missile;
    missile.id = EntityId{ports_.ids.cursor()}; missile.owner = actor.actor; missile.player = actor.player;
    missile.definition = skill.missileId; missile.area = actor.area; missile.generation = actor.areaGeneration;
    missile.created = actor.tick; missile.expires = actor.tick + uint64_t(std::ceil(skill.missileLifetime * 25.f));
    missile.position = player->position + heading * .7f; missile.velocity = heading * skill.missileVelocity;
    missile.acceleration = skill.missileAcceleration; missile.maximumVelocity = skill.missileMaxVelocity;
    missile.collision = request.collision; missile.radius = skill.missileImpact ? skill.missileImpact->radius : 0;
    missile.damage = int64_t((skill.minimumDamage + (skill.maximumDamage - skill.minimumDamage) * fraction) * 256.f);
    const auto id = missile.id;
    // Allocate the map node before committing mana. Failed commits discard the
    // provisional node without consuming identity or random state.
    auto [entry, inserted] = state_.missiles.emplace(id, std::move(missile));
    if (!inserted) return {DomainStatus::Conflict, {}};
    DomainResult<> committed;
    try { committed = ports_.transactions.release(actor, player->characterRevision, skill.manaCost); }
    catch (...) { state_.missiles.erase(entry); throw; }
    if (!committed) { state_.missiles.erase(entry); return {committed.status, {}}; }
    ports_.ids.allocate(); ports_.random = random;
    return {DomainStatus::Applied, id};
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    for (auto it = state_.missiles.begin(); it != state_.missiles.end();) {
        auto &missile = it->second;
        const auto *owner = ports_.players.find(missile.player);
        const auto *area = ports_.areas.find(missile.area);
        if (!owner || !owner->entered || owner->actor != missile.owner || owner->area != missile.area ||
            !area || area->generation != missile.generation || area->definition.town) { it = state_.missiles.erase(it); continue; }
        if (!missile.impact) {
            if (tick.tick >= missile.expires) { it = state_.missiles.erase(it); continue; }
            if (tick.tick <= missile.created) { ++it; continue; }
            // Old acceleration advances at five frames, not every render update.
            if (missile.acceleration != 0 && (tick.tick - missile.created) % 5 == 0) {
                float speed = std::max(0.f, missile.velocity.length() + missile.acceleration);
                if (speed >= missile.maximumVelocity) { speed = missile.maximumVelocity; missile.acceleration = 0; }
                missile.velocity = missile.velocity.unit() * speed;
            }
            Vec next = missile.position + missile.velocity * TickContext::seconds;
            const auto wall = missileTerrainContact(area->definition.collision, missile.position, next, missile.collision);
            if (wall) next = missile.position + (next - missile.position) * *wall;
            EntityId direct; float earliest = 2.f;
            for (const auto &[id, monster] : ports_.monsters.read().actors) {
                if (monster.life <= 0 || monster.area != missile.area) continue;
                const auto hit = missileUnitIntersection(missile.position, next, missile.collision.size, monster.position, monster.rule.size);
                if (hit && *hit < earliest) { direct = id; earliest = *hit; }
            }
            missile.position = direct ? missile.position + (next - missile.position) * earliest : next;
            ++missile.revision;
            if (direct || wall) {
                combat::SpellImpact impact{missile.id, missile.owner, missile.area, DamageType::Fire, missile.damage, {}};
                if (missile.radius > 0) {
                    // Old resolveMissileImpact tests integer subcell radii; one
                    // target list contains the direct hit too, so it is never doubled.
                    for (const auto &[id, monster] : ports_.monsters.read().actors) {
                        if (monster.life <= 0 || monster.area != missile.area) continue;
                        const auto dx = std::floor(monster.position.x) - std::floor(missile.position.x);
                        const auto dy = std::floor(monster.position.y) - std::floor(missile.position.y);
                        if (dx * dx + dy * dy <= missile.radius * missile.radius) impact.targets.push_back(id);
                    }
                } else if (direct) impact.targets.push_back(direct);
                missile.impact = std::move(impact);
            }
        }
        if (missile.impact) {
            auto result = ports_.combat.enqueue(*missile.impact);
            if (result.status == DomainStatus::Capacity) { blocked = true; ++it; }
            else it = state_.missiles.erase(it);
        } else ++it;
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}

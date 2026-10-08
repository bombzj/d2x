#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include <algorithm>

namespace d2x::server::monsters {
DomainResult<EntityId> System::admit(const Admission &request) {
    const auto *area = ports_.areas.find(request.area);
    if (!area || area->definition.town || !request.hostile || !request.rule || !request.implementation || request.identity.spawnKey.empty())
        return {DomainStatus::InvalidRequest, {}};
    const auto &rule = *request.rule;
    if (rule.nativeClass < 0 || rule.nativeClass > UINT16_MAX || rule.minimumLife <= 0 || rule.maximumLife < rule.minimumLife ||
        rule.nativeVelocity <= 0 || rule.nativeVelocity > 255 || rule.difficulty < 0 || rule.difficulty > 2 ||
        rule.level <= 0 || rule.attackTicks <= 0 || rule.impactTick <= 0 || rule.impactTick > rule.attackTicks ||
        !area->definition.collision.walkable(request.position, rule.collision)) return {DomainStatus::InvalidRequest, {}};
    if (state_.actors.size() >= 65536 || ports_.ids.cursor() >= UINT32_MAX) return {DomainStatus::Capacity, {}};
    for (const auto &[id, actor] : state_.actors)
        if (actor.area == request.area && actor.identity.spawnKey == request.identity.spawnKey) return {DomainStatus::Applied, id};
    Actor actor;
    actor.id = ports_.ids.allocate(); actor.identity = request.identity; actor.implementation = request.implementation;
    actor.area = request.area; actor.position = request.position; actor.revision = 1; actor.rule = rule;
    actor.life = actor.maximumLife = (int64_t(rule.minimumLife) + limitedRandom(ports_.random, uint32_t(rule.maximumLife - rule.minimumLife + 1))) * 256;
    const auto id = actor.id;
    state_.actors.emplace(id, std::move(actor));
    return {DomainStatus::Applied, id};
}
DomainResult<> System::requestMove(const MoveRequest &request) {
    const auto it = state_.actors.find(request.actor);
    if (it == state_.actors.end() || it->second.life <= 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    const auto &area = ports_.areas.at(actor.area);
    if (request.destination.area != actor.area || request.destination.generation != area.generation)
        return {DomainStatus::Stale, {}};
    const PlayerState *target = nullptr;
    for (const auto &[id, player] : ports_.players.all()) {
        (void)id; if (player.actor == request.target) { target = &player; break; }
    }
    if (!target || !target->entered || target->area != actor.area || target->persistent.player.hp <= 0 ||
        request.stopDistance < 0 || request.stopDistance > 255 || request.velocityPercent < 25 || request.velocityPercent > INT16_MAX)
        return {DomainStatus::InvalidRequest, {}};
    auto route = area.definition.collision.path(actor.position, request.destination.position, true, actor.rule.collision);
    if (route.empty()) return {DomainStatus::Unavailable, {}};
    actor.route = std::move(route); actor.movementTarget = request.target;
    actor.stopDistance = request.stopDistance; actor.velocityPercent = request.velocityPercent; actor.running = request.running;
    ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
void System::stop(EntityId id) {
    if (auto it = state_.actors.find(id); it != state_.actors.end()) {
        auto &actor = it->second;
        if (!actor.route.empty() || actor.moving || actor.running || actor.movementTarget) { actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; ++actor.revision; }
    }
}
DomainResult<> System::beginAttack(EntityId id, uint64_t until) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || it->second.life <= 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    actor.busyUntil = until; actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::damage(EntityId id, EntityId source, int64_t amount, uint64_t tick) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || it->second.life <= 0 || amount < 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    const auto life = std::max(int64_t(0), actor.life - amount);
    const uint8_t percent = uint8_t(life ? std::clamp<int64_t>(life * 128 / actor.maximumLife, 1, 127) : 0);
    auto event = ports_.events.publish({0, tick, {}, {AudienceKind::Area, {}, actor.area},
        {HitFact{id, 1, actor.area, percent, !life, actor.position}}});
    if (!event) return {event.status, {}};
    actor.life = life; ++actor.revision;
    if (!life) {
        actor.deathTick = tick; actor.deathOccurrence = *event.value; actor.killer = source;
        actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; actor.busyUntil = tick + uint64_t(actor.rule.deathTicks);
    }
    return {DomainStatus::Applied, std::monostate{}};
}
void System::rewardComplete(EntityId id) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.rewardComplete = true; }
DomainResult<> System::remove(EntityId) { return {}; } // Ownership/quest removal is a separate future operation.
StepStatus System::step(TickContext tick, FrameFacts &) {
    for (auto &[id, actor] : state_.actors) {
        (void)id; actor.moving = false;
        if (actor.life <= 0 || tick.tick < actor.busyUntil) continue;
        if (actor.route.empty()) continue;
        const PlayerState *target = nullptr;
        for (const auto &[playerId, player] : ports_.players.all()) {
            (void)playerId; if (player.actor == actor.movementTarget) { target = &player; break; }
        }
        if (!target || !target->entered || target->area != actor.area || target->persistent.player.hp <= 0) { stop(id); continue; }
        const auto &grid = ports_.areas.at(actor.area).definition.collision;
        auto arrived = [&](Vec position) {
            return meleeDistance(position, actor.rule.size, target->position, 2) <= actor.stopDistance &&
                grid.segment(position, target->position);
        };
        if (arrived(actor.position)) { stop(id); continue; }
        float remaining = monsterMovementSpeed(actor.rule.nativeVelocity, actor.velocityPercent) * TickContext::seconds;
        while (!actor.route.empty() && remaining > 0) {
            const Vec delta = actor.route.front() - actor.position;
            const float distance = delta.length();
            if (distance < .001f) { actor.route.pop_front(); continue; }
            const Vec next = actor.position + delta.unit() * std::min(distance, remaining);
            if (!grid.segment(actor.position, next, {}, actor.rule.collision)) { actor.route.clear(); break; }
            actor.position = next; remaining -= std::min(distance, remaining); actor.moving = true; ++actor.revision;
            if (arrived(next)) { actor.route.clear(); break; }
            if ((actor.route.front() - next).length() < .001f) actor.route.pop_front();
        }
    }
    return StepStatus::Complete;
}
}

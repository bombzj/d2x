#include "system.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/travel/system.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/behavior.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::skills {
namespace {
bool inside(const Grid &grid, Vec point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0 && point.y >= 0 &&
        point.x < grid.width && point.y < grid.height;
}
}
DomainResult<> System::cast(const ActorContext &actor, const Request &request, int selected) {
    const auto &player = *ports_.players.find(actor.player);
    const auto &area = ports_.areas.at(actor.area);
    if (!player.rules.skills) return {DomainStatus::Unavailable, {}};
    const auto &rules = *player.rules.skills;
    const auto found = rules.definitions.find(selected);
    if (found == rules.definitions.end()) return {}; // Unsupported skills never become ordinary attacks.
    if (busy(actor.actor, actor.tick)) return {DomainStatus::Conflict, {}};
    const auto &definition = found->second;
    const auto rank = player.totals.skillRanks.find(selected);
    if (rank == player.totals.skillRanks.end() || rank->second <= 0 || rank->second > UINT8_MAX || !request.target)
        return {DomainStatus::InvalidRequest, {}};
    if (area.definition.town && !definition.allowedInTown) return {DomainStatus::Unavailable, {}};
    if (definition.spec.effect != SkillBehavior::Teleport &&
        (player.totals.character.combat.lifeOnKill || player.totals.character.combat.manaOnKill)) return {};
    const auto animation = rules.animations.find(player.totals.equipment.animationClass);
    if (animation == rules.animations.end()) return {DomainStatus::Unavailable, {}};
    int64_t mastery = 0;
    for (const auto &[id, values] : rules.fireMasteries) {
        const auto learned = player.totals.skillRanks.find(id);
        if (learned != player.totals.skillRanks.end() && learned->second > 0)
            mastery += int64_t(values.first) + int64_t(learned->second - 1) * values.second;
    }
    if (mastery < 0 || mastery > INT32_MAX) return {DomainStatus::Unavailable, {}};
    // Preserve the old resolver: effective cast/mastery ranks, base synergy ranks.
    auto resolved = resolveSkill(definition.spec, {rank->second, player.persistent.player.skillRanks, int(mastery)});
    if (!std::isfinite(resolved.manaCost) || !std::isfinite(resolved.startMana) || resolved.manaCost < 0 || resolved.startMana < 0 ||
        player.persistent.player.mana < std::max(resolved.manaCost, resolved.startMana)) return {DomainStatus::Unavailable, {}};
    PointTarget target{actor.area, actor.areaGeneration, {}};
    EntityId unit;
    if (const auto *point = std::get_if<PointTarget>(&*request.target)) {
        if (point->area != actor.area || point->generation != actor.areaGeneration) return {DomainStatus::Stale, {}};
        target = *point;
    } else {
        unit = std::get<UnitTarget>(*request.target).id;
        const auto *monster = ports_.monsters.find(unit);
        if (!monster || monster->life <= 0 || monster->area != actor.area) return {DomainStatus::InvalidRequest, {}};
        target.position = monster->position;
    }
    if (!inside(area.definition.collision, target.position)) return {DomainStatus::InvalidRequest, {}};
    if (resolved.effect == SkillBehavior::Teleport && (!area.definition.teleportAllowed ||
        !area.definition.collision.walkable(target.position, playerMovement))) return {DomainStatus::Unavailable, {}};
    const auto timing = normalCastTiming(animation->second, player.totals.character.combat.fasterCast, player.totals.character.otherAnimationRate);
    if (!ports_.events.hasCapacity(1)) return {DomainStatus::Capacity, {}};
    Release release{actor, std::move(resolved), definition.collision, target, unit, actor.tick + uint64_t(timing.impact)};
    // Allocate both lifecycle nodes before publishing the start fact. No mana or
    // RNG is consumed until the authoritative action frame successfully releases.
    const auto previous = state_.casts.find(actor.actor);
    const auto saved = previous == state_.casts.end() ? std::optional<Cast>{} : previous->second;
    releases_.emplace(actor.actor, std::move(release));
    auto rollback = [&] {
        releases_.erase(actor.actor);
        if (saved) state_.casts.at(actor.actor) = *saved;
        else state_.casts.erase(actor.actor);
    };
    try {
        state_.casts[actor.actor] = {actor.actor, uint16_t(selected), actor.tick, actor.sequence,
            actor.tick + uint64_t(timing.duration), actor.area, unit};
        const auto result = ports_.events.publish({0, actor.tick, {}, {AudienceKind::Area, {}, actor.area},
            {AttackFact{actor.actor, unit, 0, 1, actor.area, player.position, target.position, actor.sequence, uint16_t(selected), uint8_t(rank->second)}}});
        if (!result) { rollback(); return {result.status, {}}; }
    } catch (...) { rollback(); throw; }
    ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
    return {DomainStatus::Applied, std::monostate{}};
}
StepStatus System::release(TickContext tick) {
    bool blocked = false;
    for (auto it = releases_.begin(); it != releases_.end();) {
        auto &pending = it->second;
        if (tick.tick < pending.tick) { ++it; continue; }
        auto actor = pending.actor; actor.tick = tick.tick;
        const auto *player = ports_.players.find(actor.player);
        const auto *area = ports_.areas.find(actor.area);
        bool valid = player && player->entered && player->actor == actor.actor && player->area == actor.area &&
            player->persistent.player.hp > 0 && area && area->generation == actor.areaGeneration &&
            player->persistent.player.mana >= std::max(pending.skill.manaCost, pending.skill.startMana);
        auto target = pending.target;
        if (valid && pending.unit) {
            const auto *monster = ports_.monsters.find(pending.unit);
            valid = monster && monster->life > 0 && monster->area == actor.area;
            if (valid) target.position = monster->position;
        }
        if (!valid) { it = releases_.erase(it); continue; }
        DomainStatus status;
        if (pending.skill.effect == SkillBehavior::Teleport)
            status = ports_.travel.teleport(actor, target, pending.skill.manaCost).status;
        else status = ports_.missiles.spawn({actor, pending.skill, pending.collision, target.position}).status;
        if (status == DomainStatus::Capacity) { blocked = true; ++it; continue; }
        if (status == DomainStatus::Applied)
            state_.casts.at(actor.actor).cooldownUntil = tick.tick + uint64_t(std::max(0, pending.skill.delayFrames));
        it = releases_.erase(it);
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}

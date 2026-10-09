#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/npc/system.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::travel {
namespace {
bool seamClear(const Grid &grid, Vec a, Vec b, int side) {
    // Only the short normal segment across a verified reciprocal seam. Grid
    // collision resolves its footprint into the linked neighbour on each cell.
    const int lateral = int(std::floor(side % 2 ? a.y : a.x));
    const int begin = int(std::floor(side % 2 ? a.x : a.y)), end = int(std::floor(side % 2 ? b.x : b.y));
    for (int normal = std::min(begin, end); normal <= std::max(begin, end); ++normal)
        if (!grid.movementClear(side % 2 ? normal : lateral, side % 2 ? lateral : normal, playerMovement)) return false;
    return true;
}
}

std::optional<CommandStatus> System::walk(const ActorContext &actor, const MovementCommand &command) {
    cancel(actor.player);
    if (command.action != MovementAction::Move) return {};
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->persistent.player.hp <= 0) return CommandStatus::InvalidBinding;
    const auto &source = ports_.areas.at(player->area);
    if (!std::isfinite(command.destination.x) || !std::isfinite(command.destination.y)) return CommandStatus::InvalidDestination;
    for (const auto &edge : source.definition.boundaries) {
        const Vec a = player->position, b = command.destination;
        const float an = edge.side % 2 ? a.x : a.y, bn = edge.side % 2 ? b.x : b.y;
        const bool positive = edge.side == 0 || edge.side == 3;
        if ((positive ? an >= edge.plane || bn < edge.plane : an < edge.plane || bn >= edge.plane) || an == bn) continue;
        const float fraction = (float(edge.plane) - an) / (bn - an);
        const Vec intersection = a + (b - a) * fraction;
        const float lateral = edge.side % 2 ? intersection.y : intersection.x;
        if (lateral < edge.start || lateral >= edge.end) continue;
        const auto *other = ports_.areas.find(edge.destination);
        if (!other) { ports_.world.requestArea(actor, edge.destination); return CommandStatus::Unavailable; }
        const Vec offset = source.definition.origin - other->definition.origin;
        const Vec target = b + offset;
        if (!other->definition.collision.walkable(target, playerMovement)) return CommandStatus::InvalidDestination;
        Vec approach = intersection;
        const float normal = float(edge.plane) + (positive ? -1.5f : 1.5f);
        if (edge.side % 2) approach.x = normal; else approach.y = normal;
        Vec arrival = intersection;
        if (edge.side % 2) arrival.x += positive ? 1.5f : -1.5f; else arrival.y += positive ? 1.5f : -1.5f;
        arrival = arrival + offset;
        if (!source.definition.collision.walkable(approach, playerMovement) || !other->definition.collision.walkable(arrival, playerMovement) ||
            !seamClear(source.definition.collision, approach, arrival - offset, edge.side)) return CommandStatus::NoRoute;
        auto route = source.definition.collision.path(a, approach, false, playerMovement);
        if (route.empty()) return CommandStatus::NoRoute;
        state_.transitions[actor.player] = {actor.area, edge.destination, 0, source.generation, actor.sequence, {}, approach, arrival, target, true, command.forceRun, false, edge.side, edge.plane};
        auto &mutablePlayer = ports_.players.players_.at(actor.player);
        mutablePlayer.route = std::move(route); mutablePlayer.routeRunning = mutablePlayer.running || command.forceRun;
        return CommandStatus::Applied;
    }
    return {};
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if (request.kind != Kind::Exit) return useSpecial(actor,request);
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0 ||
        !area || actor.areaGeneration != area->generation) return {DomainStatus::InvalidActor, {}};
    const auto found = std::find_if(area->definition.exits.begin(), area->definition.exits.end(), [&](const auto &e) { return e.id == request.source.id; });
    if (found == area->definition.exits.end() || (request.destination && *request.destination != found->destination)) return {DomainStatus::InvalidRequest, {}};
    if (found->requiresQuest) return {DomainStatus::NotImplemented, {}};
    if ((found->position - player->position).length() > 50) return {DomainStatus::InvalidRequest, {}};
    auto route = area->definition.collision.path(player->position, found->arrival, false, playerMovement);
    if (route.empty()) return {DomainStatus::Unavailable, {}};
    const auto prepared = ports_.world.requestArea(actor, found->destination);
    if (!prepared) return {prepared.status, {}};
    state_.transitions[actor.player] = {actor.area, found->destination, *prepared.value, area->generation, actor.sequence, found->id, found->arrival, {}, {}, false, false, false, 0, 0};
    auto &mutablePlayer = ports_.players.players_.at(actor.player);
    mutablePlayer.route = std::move(route); mutablePlayer.routeRunning = mutablePlayer.running;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::teleport(const ActorContext &actor, PointTarget target, float manaCost,std::optional<SkillCharge> charge) {
    const auto *player = ports_.players.find(actor.player);
    if (!player) return {DomainStatus::InvalidActor, {}};
    const auto result = ports_.transactions.release(actor, player->characterRevision, manaCost, target,charge);
    if (result) cancel(actor.player);
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    std::erase_if(state_.portals,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered;});
    std::erase_if(state_.waypoints,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered || p->area!=entry.second.area || p->persistent.player.hp<=0;});
    for(auto &[owner,portal]:state_.portals) { (void)owner; if(!portal.opened && tick.tick>=portal.ready) {portal.opened=true;++portal.revision;} }
    for (auto pending = state_.transitions.begin(); pending != state_.transitions.end();) {
        const auto found = ports_.players.players_.find(pending->first);
        auto &transition = pending->second;
        const auto *source = ports_.areas.find(transition.from), *destination = ports_.areas.find(transition.to);
        if (found == ports_.players.players_.end() || !source || source->generation != transition.sourceGeneration ||
            !found->second.entered || found->second.area != transition.from || found->second.locomotionSequence != transition.sequence || found->second.persistent.player.hp <= 0) {
            pending = state_.transitions.erase(pending); continue;
        }
        auto &player = found->second;
        if (!destination) {
            const ActorContext actor{player.player, player.actor, player.area, source->generation, transition.sequence, tick.tick};
            if (!(transition.kind==Kind::Waypoint?ports_.world.requestWaypoint(actor,transition.to):ports_.world.requestArea(actor, transition.to))) { pending = state_.transitions.erase(pending); continue; }
            ++pending; continue;
        }
        if (!player.route.empty()) { ++pending; continue; }
        if (!transition.crossing && (player.position - transition.approach).length() > .1f) { pending = state_.transitions.erase(pending); continue; }
        Vec arrival = transition.arrival;
        std::optional<Vec> exitWalk;
        if (transition.walking) {
            transition.crossing = true;
            const Vec offset = source->definition.origin - destination->definition.origin;
            const Vec across = transition.arrival - offset;
            const Vec delta = across - player.position;
            const auto speed = player.runningNow ? player.totals.character.runSpeed : player.totals.character.walkSpeed;
            const Vec next = player.position + delta.unit() * std::min(delta.length(), speed * TickContext::seconds);
            if (!seamClear(source->definition.collision, player.position, next, transition.side)) {
                player.moving = false; pending = state_.transitions.erase(pending); continue;
            }
            const float normal = transition.side % 2 ? next.x : next.y;
            const bool crossed = transition.side == 0 || transition.side == 3 ? normal >= transition.plane : normal < transition.plane;
            if (!crossed) { player.position = next; player.look = delta.unit(); player.moving = true; ++pending; continue; }
            arrival = next + offset;
        }
        if(transition.kind==Kind::SpecialPortal && std::none_of(state_.specialPortals.begin(),state_.specialPortals.end(),[&](const auto &entry){return entry.second.fieldId==transition.source || entry.second.townId==transition.source;})) {pending=state_.transitions.erase(pending);continue;}
        if (transition.kind==Kind::Portal) {
            const auto portal=state_.portals.find(player.player);
            if(portal==state_.portals.end() || (portal->second.fieldId!=transition.source && portal->second.townId!=transition.source)) { pending=state_.transitions.erase(pending); continue; }
        }
        if(transition.kind==Kind::Waypoint) {
            const auto waypoint=std::find_if(destination->definition.objects.begin(),destination->definition.objects.end(),[](const auto &object){return object.rule.operation==23;});
            if(waypoint==destination->definition.objects.end()) { pending=state_.transitions.erase(pending); continue; }
            arrival=destination->definition.collision.nearest(waypoint->position,playerMovement);
            if((arrival-waypoint->position).length()>8) { pending=state_.transitions.erase(pending); continue; }
        }
        if (!transition.walking && transition.kind==Kind::Exit) {
            const AreaExit *back = nullptr;
            for (const auto &exit : destination->definition.exits) if (exit.destination == transition.from) {
                if (back) { back = nullptr; break; } back = &exit;
            }
            if (!back) { pending = state_.transitions.erase(pending); continue; }
            arrival = back->arrival; exitWalk = back->position + back->exitWalk;
        }
        if (!destination->definition.collision.walkable(arrival, playerMovement)) { pending = state_.transitions.erase(pending); continue; }
        std::deque<Vec> route;
        if (transition.walking || exitWalk) route = destination->definition.collision.path(arrival, exitWalk.value_or(transition.destination), true, playerMovement);
        EventBatch event{0, tick.tick, {}, {AudienceKind::Player, player.player, transition.to},
            {TravelFact{player.player, player.actor, transition.from, transition.to, destination->generation, arrival, transition.walking}}};
        if (!ports_.events.publish(std::move(event))) { ++pending; continue; }
        ports_.npc.close(player.player); ports_.inventory.close(player.player); state_.waypoints.erase(player.player);
        if(transition.kind==Kind::Portal) { const auto portal=state_.portals.find(player.player); if(portal!=state_.portals.end() && portal->second.town==transition.from) state_.portals.erase(portal); }
        player.area = transition.to; player.position = arrival; player.route = std::move(route);
        player.routeRunning = player.running || transition.run; player.moving = transition.walking;
        pending = state_.transitions.erase(pending);
    }
    return state_.transitions.empty() ? StepStatus::Complete : StepStatus::Blocked;
}
}

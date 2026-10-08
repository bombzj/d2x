#include "movement.hpp"
#include "player_store.hpp"
#include "runtime/contracts.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server {
CommandStatus MovementSystem::execute(const ActorContext &actor, const MovementCommand &command) {
    const auto &ports = ports_;
    const auto found = ports.players.players_.find(actor.player);
    if (found == ports.players.players_.end()) return CommandStatus::InvalidBinding;
    auto &player = found->second;
    const auto &area = ports.areas.at(player.area);
    if (!player.entered || player.persistent.player.hp <= 0) return CommandStatus::Unavailable;
    if (player.actor != actor.actor || player.area != actor.area || area.generation != actor.areaGeneration)
        return CommandStatus::Stale;
    return applyMovement(player, area, command);
}
void MovementSystem::step(TickContext) {
    const auto &ports = ports_;
    for (auto &[id, player] : ports.players.players_) {
        (void)id;
        advanceMovement(player, ports.areas.at(player.area), TickContext::seconds);
    }
}
void MovementSystem::suspend() {
    const auto &ports = ports_;
    for (auto &[id, player] : ports.players.players_) {
        (void)id;
        player.route.clear(); player.moving = false;
    }
}
CommandStatus applyMovement(PlayerState &player, const AreaState &area, const MovementCommand &command) {
    switch (command.action) {
    case MovementAction::Stop:
        player.route.clear(); player.moving = false;
        return CommandStatus::Applied;
    case MovementAction::ToggleRunning:
        player.running = !player.running;
        player.routeRunning = player.running;
        return CommandStatus::Applied;
    case MovementAction::Move:
    case MovementAction::ApproachExit: break;
    default: return CommandStatus::Stale;
    }
    const auto &grid = area.definition.collision;
    Vec target = command.destination;
    if (command.action == MovementAction::ApproachExit) {
        const auto &exits = area.definition.exits;
        const auto found = std::find_if(exits.begin(), exits.end(), [&](const auto &exit) { return exit.id == command.exit; });
        if (found == exits.end() || std::abs(found->position.x - player.position.x) > 50 ||
            std::abs(found->position.y - player.position.y) > 50) return CommandStatus::InvalidRequest;
        target = found->arrival;
    }
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || target.x < 0 || target.y < 0 ||
        target.x >= grid.width || target.y >= grid.height) return CommandStatus::InvalidDestination;
    // Existing area navigation may stop at the closest reachable point.
    // Only the authority chooses and consumes this route.
    auto route = grid.path(player.position, target, true, playerMovement);
    if (route.empty()) {
        player.route.clear(); player.moving = false;
        return CommandStatus::NoRoute;
    }
    player.route = std::move(route);
    player.routeRunning = player.running || command.forceRun;
    return CommandStatus::Applied;
}
void advanceMovement(PlayerState &player, const AreaState &area, float seconds) {
    player.moving = false;
    if (!player.entered || player.persistent.player.hp <= 0) { player.route.clear(); return; }
    float remaining = (player.routeRunning ? player.totals.character.runSpeed : player.totals.character.walkSpeed) * seconds;
    while (remaining > 0 && !player.route.empty()) {
        const Vec delta = player.route.front() - player.position;
        const float distance = delta.length();
        if (distance < .0001f) { player.route.pop_front(); continue; }
        const float step = std::min(remaining, distance);
        const Vec next = player.position + delta * (step / distance);
        if (!area.definition.collision.segment(player.position, next, {}, playerMovement)) {
            player.route.clear(); break;
        }
        player.look = delta.unit(); player.position = next; player.moving = true;
        remaining -= step;
        if (step >= distance) player.route.pop_front();
    }
}
} // namespace d2x::server

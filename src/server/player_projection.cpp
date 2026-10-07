#include "player_projection.hpp"

namespace d2x::server {
PlayerSnapshot projectPlayer(const PlayerState &p) {
    PlayerSnapshot result;
    result.recipient = p.player; result.command = p.result;
    result.movementSequence = p.movementSequence;
    auto &actor = result.actor;
    actor.id = p.actor; actor.region = p.area; actor.position = p.position; actor.look = p.look;
    actor.nextPosition = p.route.empty() ? p.position : p.route.front();
    actor.moving = p.moving; actor.running = p.routeRunning;
    actor.movementSpeed = p.routeRunning ? p.attributes.runSpeed : p.attributes.walkSpeed;
    return result;
}
} // namespace d2x::server

#include "player_projection.hpp"
#include "server/systems/inventory/item_skills.hpp"

namespace d2x::server {
PlayerSnapshot projectPlayer(const PlayerState &p) {
    PlayerSnapshot result;
    result.recipient = p.player; result.command = p.result;
    result.movementSequence = p.movementSequence;
    result.inventoryRevision = p.inventoryRevision; result.characterRevision = p.characterRevision;
    result.name = p.persistent.player.name; result.characterClass = p.persistent.player.characterClass;
    result.states = p.transient.states; result.itemSkills=inventory::itemSkills(p);
    result.level = p.persistent.player.level; result.entered = p.entered;
    result.life = p.persistent.player.hp;
    result.attributes = p.totals.character; result.equipment = p.totals.equipment; result.skillRanks = p.totals.skillRanks;
    auto &actor = result.actor;
    actor.id = p.actor; actor.region = p.area; actor.position = p.position; actor.look = p.look;
    actor.nextPosition = p.route.empty() ? p.position : p.route.front();
    actor.moving = p.moving && p.persistent.player.hp > 0; actor.running = p.runningNow;
    actor.movementSpeed = p.runningNow ? p.totals.character.runSpeed : p.totals.character.walkSpeed;
    return result;
}
} // namespace d2x::server

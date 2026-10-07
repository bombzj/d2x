#include "player_store.hpp"
#include "area_store.hpp"
#include <stdexcept>
#include <utility>

namespace d2x::server {
void PlayerStore::admit(PlayerId id, CharacterDefinition definition,
                        PersistentCharacter persistent, const AreaStore &areas) {
    if (!id.value || players_.contains(id))
        throw std::runtime_error("Invalid or duplicate admitted player");
    PlayerState player;
    player.player = id; player.actor = persistent.player.id;
    if (!player.actor) throw std::runtime_error("Game admission requires a character");
    player.area = persistent.lastRegion;
    player.position = areas.at(player.area).definition.spawn;
    player.persistent = std::move(persistent);
    player.definition = std::move(definition);
    if (player.definition.walkVelocity <= 0 || player.definition.runVelocity <= 0)
        throw std::runtime_error("Missing prepared movement rules");
    player.attributes = deriveCharacterAttributes(player.definition,
        player.persistent.player.level, player.persistent.player.allocated);
    players_.emplace(id, std::move(player));
}
}

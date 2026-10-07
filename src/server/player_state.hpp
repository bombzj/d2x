#pragma once
#include "server/game_messages.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/character/persistent_character.hpp"
#include <deque>

namespace d2x::server {
struct PlayerState {
    PlayerId player;
    EntityId actor;
    RegionId area = RegionId::Encampment;
    CharacterDefinition definition;
    CharacterAttributes attributes;
    PersistentCharacter persistent;
    Vec position, look{0, 1};
    std::deque<Vec> route;
    bool running{}, routeRunning{}, moving{};
    uint64_t acceptedSequence{}, movementSequence{};
    CommandResult result;
};
} // namespace d2x::server

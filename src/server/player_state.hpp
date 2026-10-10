#pragma once
#include "server/game_messages.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/character/persistent_character.hpp"
#include <deque>

namespace d2x::server {
struct PlayerState {
    PlayerId player;
    EntityId actor;
    RegionId area = RegionId::Encampment;
    CharacterDefinition definition;
    attributes::Totals totals;
    PersistentCharacter persistent;
    TransientAttributes transient;
    PreparedRules rules; // Per-character properties and class growth; immutable after admission.
    bool entered{};
    Vec position, look{0, 1};
    std::deque<Vec> route;
    bool running{}, routeRunning{}, runningNow{}, moving{};
    uint64_t acceptedSequence{}, movementSequence{}, locomotionSequence{};
    uint64_t teleportRevision{};
    uint64_t inventoryRevision = 1, characterRevision = 1;
    uint64_t lastExperienceAward{};
    CommandResult result;
};
} // namespace d2x::server

#pragma once
#include "server/game_commands.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "world/native_path_step.hpp"
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
    uint8_t targetNodeIndex{11}; // First free native player target group, runtime only.
    Vec position, look{0, 1};
    std::deque<Vec> route;
    NativePathStep pathStep;
    // PlrModes: twenty global native positions, sampled after squared travel >45.
    // Runtime follow history only; never serialized with the character.
    std::deque<Vec> movementHistory;
    Vec lastTeleportPosition; // Native PlayerData path-history slots 40/41, global coordinates.
    std::optional<MotionTarget> movementTarget;
    std::optional<InteractionTarget> movementArrival;
    Vec routeDestination;
    uint64_t routeObstacles{};
    bool running{}, routeRunning{}, runningNow{}, moving{};
    uint64_t acceptedSequence{}, movementSequence{}, locomotionSequence{};
    uint64_t teleportRevision{};
    uint64_t inventoryRevision = 1, characterRevision = 1;
    uint64_t lastExperienceAward{};
    uint64_t combatRandom{}; // Native unit seed, runtime only.
    CommandResult result;
};
} // namespace d2x::server

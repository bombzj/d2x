#pragma once
#include "gameplay/combat/identity.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/character/state.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/npc/hireling.hpp"
#include "gameplay/player/resources_state.hpp"
#include "gameplay/player/movement_state.hpp"
#include "gameplay/player/action_state.hpp"
#include "gameplay/player/skill_state.hpp"

namespace d2x {
// One owner for live character data. Save encoding consumes CharacterRecord only.
struct PlayerState {
    EntityId id;
    CombatIdentity allegiance{1, {}, 0, CombatRole::Player};
    CharacterState character;
    PlayerResources resources;
    PlayerMovement movement;
    PlayerActions actions;
    PlayerSkills skills;
    CharacterAttributes attributes;
    EquipmentStats equipment;
    CombatEffectSet combatEffects;
    uint64_t combatRandom = 0;
    HirelingState hireling;
};
} // namespace d2x

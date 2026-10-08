#pragma once
#include "contracts.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace d2x::server {
enum class SystemPhase { Commands, World, Attributes, Decisions, Actions, Actors, Spatial, Missiles, Effects, Combat, Death, Objects, Quests, Rewards, Travel, Commit, Projection };
enum class SystemScope { Scaffold, Walking, Inventory, Character, World, Multiplayer, Combat, Implemented };
enum class SystemId {
    Players, Movement,
#define D2X_SYSTEM(id, member, phase, scope) id,
#include "subsystems.inc"
#undef D2X_SYSTEM
    Count
};
struct SystemDescriptor { SystemId id; std::string_view name; SystemPhase phase; SystemScope scope; };
std::span<const SystemDescriptor> systemCatalog();
std::string_view phaseName(SystemPhase);
std::string_view scopeName(SystemScope);
std::string_view stepStatusName(StepStatus);
}

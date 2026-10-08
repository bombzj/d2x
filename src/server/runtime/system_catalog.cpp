#include "system_catalog.hpp"
#include <stdexcept>

namespace d2x::server {
std::span<const SystemDescriptor> systemCatalog() {
    static constexpr SystemDescriptor entries[]{
        {SystemId::Players, "players", SystemPhase::Commands, SystemScope::Walking},
        {SystemId::Movement, "movement", SystemPhase::Actors, SystemScope::Walking},
#define D2X_SYSTEM(id, member, phase, scope) {SystemId::id, #member, SystemPhase::phase, SystemScope::scope},
#include "subsystems.inc"
#undef D2X_SYSTEM
    };
    return entries;
}
std::string_view phaseName(SystemPhase phase) {
    switch (phase) {
#define PHASE(name) case SystemPhase::name: return #name;
    PHASE(Commands) PHASE(World) PHASE(Attributes) PHASE(Decisions) PHASE(Actions)
    PHASE(Actors) PHASE(Spatial) PHASE(Missiles) PHASE(Combat) PHASE(Effects) PHASE(Death)
    PHASE(Objects) PHASE(Quests) PHASE(Rewards) PHASE(Travel) PHASE(Commit) PHASE(Projection)
#undef PHASE
    }
    throw std::logic_error("Unknown system phase");
}
std::string_view scopeName(SystemScope scope) {
    switch (scope) {
    case SystemScope::Scaffold: return "scaffold";
    case SystemScope::Walking: return "walking-slice";
    case SystemScope::Inventory: return "inventory-slice";
    case SystemScope::Character: return "character-slice";
    case SystemScope::World: return "world";
    case SystemScope::Multiplayer: return "multiplayer";
    case SystemScope::Combat: return "combat-slice";
    case SystemScope::Implemented: return "implemented";
    }
    throw std::logic_error("Unknown system scope");
}
std::string_view stepStatusName(StepStatus status) {
    switch (status) {
    case StepStatus::Complete: return "complete";
    case StepStatus::NotImplemented: return "not-implemented";
    case StepStatus::Blocked: return "blocked";
    }
    throw std::logic_error("Unknown step status");
}
}

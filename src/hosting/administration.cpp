#include "administration.hpp"
#include <stdexcept>

namespace d2x::hosting {
std::span<const AdminDescriptor> adminCommands() {
    static constexpr AdminDescriptor commands[]{
        {AdminOperation::Save, "save", AdminArgumentKind::None, true},
        {AdminOperation::Reload, "load", AdminArgumentKind::None, true},
        {AdminOperation::CancelReload, "cancel-load", AdminArgumentKind::None, true},
        {AdminOperation::Step, "step", AdminArgumentKind::Step, true},
        {AdminOperation::GrantGold, "grant-gold", AdminArgumentKind::Amount, false},
        {AdminOperation::GrantExperience, "grant-experience", AdminArgumentKind::Amount, false},
        {AdminOperation::SpawnItem, "item-spawn", AdminArgumentKind::Spawn, false},
        {AdminOperation::SpawnMonster, "monster-spawn", AdminArgumentKind::Spawn, false},
        {AdminOperation::DamageMonster, "monster-damage", AdminArgumentKind::Unit, false},
        {AdminOperation::KillMonster, "monster-kill", AdminArgumentKind::Unit, false},
        {AdminOperation::Travel, "travel", AdminArgumentKind::Travel, false},
        {AdminOperation::UnlockWaypoints, "unlock-waypoints", AdminArgumentKind::None, false},
        {AdminOperation::GrantShrine, "grant-shrine", AdminArgumentKind::Spawn, false},
        {AdminOperation::GrantHireling, "grant-hireling", AdminArgumentKind::Spawn, false},
        {AdminOperation::ResetAttributes, "reset-attributes", AdminArgumentKind::None, false},
        {AdminOperation::ResetSkills, "reset-skills", AdminArgumentKind::None, false},
    };
    return commands;
}
std::string_view adminStatusName(AdminStatus status) {
    switch (status) {
    case AdminStatus::Applied: return "applied";
    case AdminStatus::NotImplemented: return "not-implemented";
    case AdminStatus::InvalidTarget: return "invalid-target";
    case AdminStatus::InvalidArguments: return "invalid-arguments";
    case AdminStatus::Unavailable: return "unavailable";
    case AdminStatus::Failed: return "failed";
    }
    throw std::logic_error("Invalid administration result");
}
std::string_view commandStatusName(CommandStatus status) {
    switch (status) {
    case CommandStatus::Queued: return "queued";
    case CommandStatus::Applied: return "applied";
    case CommandStatus::NotImplemented: return "not-implemented";
    case CommandStatus::InvalidRequest: return "invalid-request";
    case CommandStatus::Unavailable: return "unavailable";
    case CommandStatus::Conflict: return "conflict";
    case CommandStatus::InvalidBinding: return "invalid-binding";
    case CommandStatus::Stale: return "stale";
    case CommandStatus::Paused: return "paused";
    case CommandStatus::QueueFull: return "queue-full";
    case CommandStatus::InvalidDestination: return "invalid-destination";
    case CommandStatus::NoRoute: return "no-route";
    }
    throw std::logic_error("Invalid command result");
}
}

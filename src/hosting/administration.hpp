#pragma once
#include "server/game_messages.hpp"
#include "server/runtime/simulation.hpp"
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace d2x::hosting {
// A host control port. Never encoded as private MCP/D2GS extensions and never
// available on a remote game connection. Execute on the host scheduler thread.
enum class AdminOperation {
    Save, Reload, CancelReload, Step, GrantGold, GrantExperience, SpawnItem, SpawnMonster,
    DamageMonster, KillMonster, Travel, UnlockWaypoints, GrantShrine,
    GrantHireling, ResetAttributes, ResetSkills
};
struct AdminAmount { int64_t value{}; };
struct AdminStep { uint32_t frames = 1; };
struct AdminSpawn { std::string code; int level = 1; };
struct AdminUnit { uint64_t id{}; int64_t amount{}; };
struct AdminTravel { int level{}; };
using AdminArguments = std::variant<std::monostate, AdminAmount, AdminStep, AdminSpawn, AdminUnit, AdminTravel>;
struct AdminRequest {
    AdminOperation operation;
    PlayerBinding target;
    AdminArguments arguments;
};
enum class AdminStatus { Applied, NotImplemented, InvalidTarget, InvalidArguments, Unavailable, Failed };
struct AdminResult {
    AdminStatus status;
    std::string message;
    bool applied() const { return status == AdminStatus::Applied; }
};
enum class AdminArgumentKind { None, Amount, Step, Spawn, Unit, Travel };
struct AdminDescriptor {
    AdminOperation operation;
    std::string_view command;
    AdminArgumentKind arguments;
    bool implemented;
};
std::span<const AdminDescriptor> adminCommands();
std::string_view adminStatusName(AdminStatus);
std::string_view commandStatusName(CommandStatus);
struct PacketCounters {
    uint64_t received{}, completed{}, queued{}, notImplemented{}, rejected{}, malformed{};
};
struct HostDiagnostics {
    std::optional<PlayerBinding> player;
    uint64_t tick{};
    uint64_t failures{};
    bool paused{};
    std::optional<CommandResult> command;
    std::optional<server::SystemSteps> systemSteps;
    std::string phase;
    std::array<PacketCounters, 256> realmRequests{}, gameRequests{};
    std::array<uint64_t, 256> realmResponses{}, gameResponses{};
    std::string lastRequest, lastResult;
    std::string lastFailure;
    std::vector<std::string> characterIssues;
};
}

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
    Save, Reload, CancelReload, Step, Pause, Resume, AutoPause, RestoreResources, DamagePlayer, MissileHit, GrantGold, GrantExperience, SpawnItem, SpawnMonster,
    DamageMonster, KillMonster, Travel, UnlockWaypoints, GrantShrine,
    GrantHireling, ResetAttributes, ResetSkills
};
struct AdminAmount { int64_t value{}; };
struct AdminStep { uint32_t frames = 1; };
struct AdminSpawn { std::string code; int level = 1; std::optional<Vec> position; std::string quality = "normal"; std::optional<unsigned> durability = {}; };
struct AdminMissile {uint64_t source{};uint32_t amount{};int missile{};};
struct AdminUnit { uint64_t id{}; int64_t amount{}; };
struct AdminTravel { int level{}; };
using AdminArguments = std::variant<std::monostate, AdminAmount, AdminStep, AdminSpawn, AdminUnit, AdminTravel, AdminMissile>;
struct AdminRequest {
    AdminOperation operation;
    PlayerBinding target;
    AdminArguments arguments;
};
enum class AdminStatus { Applied, NotImplemented, InvalidTarget, InvalidArguments, Unavailable, Failed };
struct AdminResult {
    AdminStatus status;
    std::string message;
    std::optional<EntityId> entity = {};
    bool applied() const { return status == AdminStatus::Applied; }
};
enum class AdminArgumentKind { None, Amount, Step, Spawn, Unit, Travel, Missile };
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
struct HostParticipant {
    PlayerBinding binding;
    std::string name, phase, failure;
    RegionId area{};
    bool entered{};
};
struct HostRoom { GameHandle handle; std::string name; unsigned capacity{}, participants{}; };
struct HostDiagnostics {
    std::optional<PlayerBinding> player;
    std::vector<HostRoom> rooms;
    std::vector<HostParticipant> participants;
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

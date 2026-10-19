#pragma once
#include "server/game_messages.hpp"
#include "server/runtime/simulation.hpp"
#include "fixed_step_job.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/items/generation.hpp"
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
    Save, Reload, CancelReload, Step, CancelStep, Pause, Resume, AutoPause, RestoreResources, DamagePlayer, MissileHit, GrantGold, GrantExperience, SpawnItem, SpawnMonster, SpawnObject,
    DamageMonster, KillMonster, Travel, UnlockWaypoints, GrantShrine,
    GrantHireling, ResetAttributes, ResetSkills, SetPlayers
};
struct AdminAmount { int64_t value{}; };
struct AdminStep { uint32_t frames = 1; };
struct AdminItemAffix { bool prefix{}; int row{}; std::optional<std::vector<int32_t>> rolls; };
struct AdminSpawn { std::string code; int level = 1; std::optional<Vec> position; std::string quality = "normal"; std::optional<unsigned> durability = {}; MonsterRank rank{MonsterRank::Normal}; std::string superUnique{}; std::optional<unsigned> sockets{}; bool ethereal{}; std::optional<int> shrine{},trap{}; std::optional<uint32_t> seed{}; std::vector<AdminItemAffix> affixes{}; std::vector<ItemPropertyTarget> properties{}; bool preview{}; std::optional<std::string> propertyQuery; };
struct AdminMissile {uint64_t source{};uint32_t amount{};int missile{};};
struct AdminUnit { uint64_t id{}; int64_t amount{}; };
struct AdminTravel { int level{}; std::optional<Vec> position; };
struct AdminWaypoints { std::optional<int> level; bool all{}; };
using AdminArguments = std::variant<std::monostate, AdminAmount, AdminStep, AdminSpawn, AdminUnit, AdminTravel, AdminMissile, AdminWaypoints>;
struct AdminRequest {
    AdminOperation operation;
    PlayerBinding target;
    AdminArguments arguments;
};
enum class AdminStatus { Applied, NotImplemented, InvalidTarget, InvalidArguments, Unavailable, Failed };
struct AdminItemDescription {
    struct Affix { bool prefix{}; int row{}; std::string name; std::vector<int32_t> rolls; };
    struct Stat { std::string name; int id{},parameter{},value{}; };
    std::string code,quality;
    uint32_t seed{},itemSeed{};
    int level{},requiredLevel{},defense{};
    unsigned durability{},maximumDurability{},sockets{};
    bool preview{},ethereal{};
    std::vector<Affix> affixes;
    std::vector<Stat> stats;
};
struct AdminPropertyCandidate {
    struct Property { std::string code,parameter; int minimum{},maximum{}; bool direct{}; };
    bool prefix{};
    int row{},group{},requiredLevel{};
    std::string name;
    std::vector<Property> properties;
};
struct AdminResult {
    AdminStatus status;
    std::string message;
    std::optional<EntityId> entity = {};
    std::optional<AdminItemDescription> item = {};
    std::optional<std::vector<AdminPropertyCandidate>> propertyCandidates = {};
    bool applied() const { return status == AdminStatus::Applied; }
};
enum class AdminArgumentKind { None, Amount, Step, Spawn, Unit, Travel, Missile, Waypoints };
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
    std::optional<FixedStepProgress> fixedSteps;
    std::string phase;
    std::array<PacketCounters, 256> realmRequests{}, gameRequests{};
    std::array<uint64_t, 256> realmResponses{}, gameResponses{};
    std::string lastRequest, lastResult;
    std::string lastFailure;
    std::vector<std::string> characterIssues;
};
}

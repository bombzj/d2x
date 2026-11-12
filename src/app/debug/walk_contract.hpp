#pragma once
#include "contracts/online_world.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <nlohmann/json_fwd.hpp>

namespace d2x {
enum class DebugWalkGoal {None,Point,Unit,Waypoint,Exit};
enum class DebugWalkState {Idle,Searching,Moving,Interacting,Completed,Failed,Cancelled,NeedsObservation,NeedsReplan};
// Classification belongs to the producer. Display text never selects recovery.
enum class DebugWalkFailure {None,Other,TargetWithdrawn,NoProgress,NoReachableSegment,InteractionUnanswered,ApproachUnanswered};
bool recoverableDebugWalkFailure(DebugWalkFailure);
std::string_view debugWalkName(DebugWalkGoal);
std::string_view debugWalkName(DebugWalkState);

// Optional flags preserve intent identity: an omitted option and an explicit
// false were distinct in the original command. Metadata belongs to the parent
// intent; the walking executor consumes only its movement/interaction options.
struct DebugWalkIntent {
    DebugWalkGoal goal=DebugWalkGoal::None;
    std::optional<OnlinePoint> point{};
    // Search bearing only; real assignments and acquired collision still
    // determine the target, route and completion.
    std::optional<OnlinePoint> searchHint{};
    std::optional<OnlineUnitKey> unit{};
    std::optional<uint16_t> level{};
    std::optional<bool> interact{},run{},exploration{},exactPoint{},recoverableRelay{},adjustLane{};
    std::optional<bool> forceApproach{},resyncApproach{},changeStance{},waitQuestAlert{},confirmMenu{},localCrossing{};
    std::optional<std::string> purpose{};
    std::optional<uint16_t> waypointArea{};
    std::optional<bool> activation{},directTeleport{};
    std::optional<uint32_t> itemId{};
    std::optional<uint16_t> groundX{},groundY{};
    std::optional<int> timeoutSeconds{};
    // A confirmed Teleport releases this relay to its parent for continuation;
    // actual goal arrival and interaction still require their own observations.
    bool routeRelay{};
    bool prepared{};
    explicit operator bool() const {return goal!=DebugWalkGoal::None;}
    bool operator==(const DebugWalkIntent &) const = default;
};
void to_json(nlohmann::json &,const DebugWalkIntent &);

struct DebugWalkSubmission {
    uint64_t worldRevision{},discontinuity{};
    OnlinePoint position;
};
// Small observation value; no path history, terrain or diagnostic JSON copies.
struct DebugWalkObservation {
    DebugWalkState state{};
    DebugWalkFailure failure{};
    std::string reason;
    uint64_t gameGeneration{},areaGeneration{},interactionRevision{};
    std::optional<OnlinePoint> target,crossingTarget,recoveryAnchor;
    std::optional<OnlineUnitKey> unit;
    std::optional<DebugWalkSubmission> submittedInteraction;
    unsigned moveRequests{},interactionRequests{},movementRecoveries{};
    uint64_t searchContinuations{};
};
} // namespace d2x

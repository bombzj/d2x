#pragma once
#include "contracts/online_scene.hpp"
#include "action_state.hpp"
#include "decision_cadence.hpp"
#include "navigation_search.hpp"
#include <chrono>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <set>
#include <string>

namespace d2x {
struct DebugWalkTiming {
  int walkDecisionMs=160,walkReactionMs=40,walkRequestIntervalMs=200,walkRetryMs=1000;
  int townTurnBaseMs=200,townTurnMinMs=600,townTurnMaxMs=2000;
};
struct Grid;
class RemoteTown;
class RemoteControl;
class RemoteCombat;
namespace net {class RealmSession;}
// Explicit debug input driver. Never reads host state or writes replica positions.
class DebugAutoWalk {
    using Clock=std::chrono::steady_clock;
    enum class Goal {Point,Unit,Waypoint,Exit} goal_{};
    std::string state_="idle",reason_,source_;
    uint64_t game_{},area_{};
    std::optional<uint16_t> level_;
    std::optional<uint16_t> destination_;
    std::optional<OnlinePoint> point_,segment_,previous_,client_,server_,edgeStart_,edgeEnd_;
    std::optional<OnlinePoint> lastFrom_,lastRequest_,lastTarget_;
    std::optional<OnlinePoint> recoveryAnchor_,hostProgressPosition_;
    Clock::time_point hostProgressAt_{};
    uint64_t hostProgressRevision_{};
    unsigned movementRecoveries_{};
    bool resyncApproach_{},changeStance_{},closeApproach_{},forceApproach_{},waitQuestAlert_{},waypointOpening_{},waypointMenu_{};
    unsigned approachRetries_{};
    Clock::time_point clickedAt_{},alertWaitSince_{};
    Clock::time_point townTurnSince_{};
    uint64_t legDiscontinuity_{};
    uint64_t interactionRevision_{},interactionDiscontinuity_{};
    std::optional<OnlinePoint> interactionFrom_;
    std::optional<uint16_t> exitLevel_;
    std::optional<OnlineUnitKey> unit_;
    std::set<std::pair<int,int>> visited_;
    // Failed speculative approaches, per destination in the current scene.
    std::map<uint16_t,std::map<std::pair<int,int>,size_t>> exhaustedExitFrontiers_;
    Clock::time_point deadline_,progressAt_,submittedAt_;
    DebugExecutionClock executionClock_;
    DebugDecisionCadence cadence_;
    DebugWalkTiming timing_;
    unsigned moves_{},interactions_{},searches_{},decisions_{};
    uint64_t failures_{};
    bool run_=true,interact_{},landmark_{},clientMoving_{},exploration_{},localCrossing_{};
    DebugNavigationSearch search_;
    void finish(std::string,std::string,RemoteControl &);
    nlohmann::json status() const;
  public:
    void setTiming(const DebugWalkTiming &timing) {timing_=timing;cadence_.wake();}
    DebugNavigationSearch &navigationSearch() {return search_;}
    bool active() const;
    bool recoveringMovement() const {return active() && recoveryAnchor_.has_value();}
    bool blockedByUnit() const {return active() && reason_=="Next movement segment temporarily occupied";}
    uint64_t failureCount() const {return failures_;}
    nlohmann::json snapshot() const;
    std::optional<std::string> command(const nlohmann::json &,net::RealmSession &,RemoteTown &,RemoteControl &,RemoteCombat &);
    void tick(net::RealmSession &,RemoteTown &,RemoteControl &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
    void manualInput(RemoteControl &);
};
}

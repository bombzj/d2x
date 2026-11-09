#pragma once
#include "contracts/online_scene.hpp"
#include "action_state.hpp"
#include "decision_cadence.hpp"
#include "navigation_search.hpp"
#include <chrono>
#include <deque>
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
    std::optional<OnlinePoint> point_,segment_,previous_,client_,server_,edgeStart_,edgeEnd_,crossing_;
    std::optional<OnlinePoint> lastFrom_,lastRequest_,lastTarget_;
    std::optional<Vec> lastPointerFeet_;
    struct TownLane {
        OnlinePoint bend,resume,target;
        bool resuming{};
    };
    std::optional<TownLane> townLane_;
    struct PointerSelection {
        unsigned request{};
        OnlinePoint from,target,raw,lane,submitted;
        Vec feet;
        std::optional<OnlinePoint> resume;
        std::string branch;
    };
    std::deque<PointerSelection> pointerSelections_;
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
    std::optional<OnlineUnitKey> routeDoor_;
    Clock::time_point routeDoorSince_{},routeDoorRequest_{};
    std::set<std::pair<int,int>> visited_;
    // Distant openings that stalled, keyed to local observed collision; retried when that evidence changes.
    std::map<std::pair<int,int>,size_t> deferredOpenings_;
    std::optional<uint64_t> passage_;
    uint64_t passageEvidence_{};
    std::optional<Clock::time_point> passageObservationSince_;
    std::optional<OnlinePoint> passageTarget_;
    Clock::time_point deadline_,progressAt_,submittedAt_;
    // Point goals: closest observed distance and when it last improved by a cell.
    float closestPoint_{};Clock::time_point closestAt_{};
    std::optional<OnlinePoint> relayClient_,relayHost_;
    std::shared_ptr<const NavigationSnapshot> relayTerrain_;
    Clock::time_point relayProgressAt_{};
    unsigned relayProgressMoves_{};
    bool recoverableRelay_{},adjustLane_=true;
    DebugExecutionClock executionClock_;
    DebugDecisionCadence cadence_;
    DebugWalkTiming timing_;
    unsigned moves_{},interactions_{},searches_{},decisions_{};
    uint64_t failures_{};
    bool run_=true,interact_{},landmark_{},clientMoving_{},exploration_{},localCrossing_{},exactPoint_{};
    DebugNavigationSearch search_;
    void finish(std::string,std::string,RemoteControl &);
    nlohmann::json status() const;
  public:
    void setTiming(const DebugWalkTiming &timing) {timing_=timing;cadence_.wake();}
    DebugNavigationSearch &navigationSearch() {return search_;}
    bool active() const;
    bool recoveringMovement() const {return active() && recoveryAnchor_.has_value();}
    bool blockedByUnit() const {return active() && reason_=="Next movement segment temporarily occupied";}
    bool openingRouteDoor() const {return active() && routeDoor_.has_value();}
    uint64_t failureCount() const {return failures_;}
    nlohmann::json snapshot() const;
    std::optional<std::string> command(const nlohmann::json &,net::RealmSession &,RemoteTown &,RemoteControl &,RemoteCombat &);
    void tick(net::RealmSession &,RemoteTown &,RemoteControl &,bool blocked,std::optional<Vec> pathPosition,bool pathMoving);
    void manualInput(RemoteControl &);
};
}

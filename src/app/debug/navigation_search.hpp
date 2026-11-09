#pragma once
#include <utility>
#include "exploration_memory.hpp"
#include "world/navigation.hpp"
#include "world/navigation_planner.hpp"
#include "contracts/online_world.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <source_location>
#include <set>
#include <string>
#include <tuple>

namespace d2x {
class RemoteTown;
struct OnlineView;
struct OnlineSceneView;
nlohmann::json debugMapReadiness(const OnlineSceneView &);
// A pending background result is distinct from an empty completed route.
struct DebugSearchPending {};
class DebugNavigationSearch {
    using Clock=std::chrono::steady_clock;
    using Key=std::tuple<uint64_t,uint64_t,bool,int,int,uint16_t,int,bool,bool,float,float,float,float>;
    struct Result {std::deque<Vec> path;uint64_t used{};};
    std::map<Key,Result> results_;
    // Reachability only: a prefix of an already checked route is a proof,
    // not a claim that it is the shortest route to another candidate.
    std::map<Key,Key> reachable_;
    static constexpr int wallBlock=32;
    std::map<std::pair<int,int>,std::array<uint8_t,wallBlock*wallBlock>> wallDistances_;
    std::array<uint64_t,3> context_{};
    size_t points_{};
    uint64_t frames_{},queries_{},hits_{},proofHits_{},yields_{},resets_{},segmentReuses_{};
    unsigned frameQueries_{},frameHits_{};
    double frameMicros_{},totalMicros_{},maxMicros_{};
    std::string lastFile_,slowestFile_;
    uint_least32_t lastLine_{},slowestLine_{};
    Vec slowestFrom_{},slowestTo_{};
    class Worker;
    std::unique_ptr<Worker> worker_;
    [[noreturn]] void defer(bool travel=false);
    uint64_t componentSearches_{},routeJoins_{};
    struct Anchor {Vec from; Clock::time_point touched;};
    std::map<std::pair<float,float>,Anchor> travelAnchors_;
    struct WalkingRoute {
        NavigationRoute route;
        uint64_t geometry{},used{};
        int width{},height{};
        std::optional<std::pair<int,int>> origin;
        std::shared_ptr<const NavigationSnapshot> snapshot;
    };
    std::map<std::pair<float,float>,WalkingRoute> walkingRoutes_;
    Clock::time_point lastDeferred_{},waitingSince_{};
    uint64_t lastDeferredFrame_{};

    bool yielded_{};
    std::set<uint16_t> doorClasses_;
    std::shared_ptr<const NavigationSnapshot> planningSource_,planningSnapshot_;
    std::shared_ptr<const Grid> planningGrid_;
    std::set<EntityId> planningDoors_;
    std::shared_ptr<const DebugExplorationMemory::Execution> execution_;
    uint64_t executionRevision_{},executionTerrain_{};
    uint16_t executionArea_{};
    uint64_t observedQueries_{},observedHits_{},observedRevalidated_{},observedSuffixes_{},observedRejected_{};
    double observedSnapshotMs_{},observedValidationMs_{};
  public:
    DebugExplorationMemory explorationMemory;
    DebugExplorationMemory::Area *observeTerrain(const RemoteTown &,const OnlineView &,Vec position);
    // Advisory travel scoring never suspends an executable action.
    std::optional<float> estimatedRouteCost(uint16_t area,Vec from,Vec target);
    DebugExplorationMemory::Plan observedRoute(uint16_t area,Vec from,int range=0,bool restricted=false,
        std::optional<Vec> target={},float vicinity=2,uint64_t committed=0,bool exploreGoal=false,
        std::optional<std::vector<DebugExplorationMemory::Cell>> approaches={},
        std::optional<DebugExplorationMemory::Cell> preferredDirection={},
        std::source_location caller=std::source_location::current());
    std::vector<DebugExplorationMemory::Cell> interactionApproaches(const RemoteTown &,const OnlineView &,OnlineUnitKey);
    struct WalkingOpening {
        OnlinePoint point,boundaryStart,boundaryEnd;
        std::optional<OnlinePoint> crossing; // Empty until the opposite bank is acquired.
        uint16_t destination{};
    };
    struct WalkingConnection {
        std::optional<OnlinePoint> bearing;
        std::vector<WalkingOpening> openings;
    };
    WalkingConnection walkingConnection(const RemoteTown &,Vec from,std::optional<uint16_t> destination);
    static bool observationReady(const RemoteTown &,const OnlineView &,Vec destination,
        std::optional<std::chrono::steady_clock::time_point> &since);
    static bool passageObservationReady(const RemoteTown &,const OnlineView &,
        const DebugExplorationMemory::Passage &,Vec destination,
        std::optional<std::chrono::steady_clock::time_point> &since);
    DebugNavigationSearch();
    ~DebugNavigationSearch();
    void setDoorClasses(std::set<uint16_t> classes);
    bool ordinaryDoor(uint16_t identity) const {return doorClasses_.contains(identity);}
    const Grid &planningGrid(const RemoteTown &,const OnlineView &);
    Grid planningCollision(const RemoteTown &,const OnlineView &,std::optional<OnlineUnitKey> ignored);
    using TravelRoute=NavigationRoute;
    using TeleportRoom=NavigationRoom;
    using Candidate=NavigationTarget;
    using CandidateRoute=NavigationSelection;
    enum class PlanningPurpose {Exploration,Waypoint,Interaction,ExitOpening,ExitFrontier,Backtrack};
    CandidateRoute selectCandidate(const Grid &,Vec from,const std::vector<Candidate> &,
        const std::vector<TeleportRoom> &rooms={},int range=0,bool restricted=false,Vec origin={},
        PlanningPurpose purpose=PlanningPurpose::Exploration);
    // Local cell centers, static terrain only. Execution checks the current
    // native room neighbourhood and live landing occupancy separately.
    TravelRoute teleportRoute(const Grid &,const std::vector<TeleportRoom> &,Vec from,Vec to,int range,bool restricted,Vec coordinateOrigin);
    void discardTeleportRoute(Vec to,int range,bool restricted);
    void rememberTeleportLanding(Vec to,int range,bool restricted,Vec waypoint,Vec landing);
    void confirmTeleportLanding(Vec requested,Vec arrived);
    TravelRoute travelRoute(const Grid &terrain,Vec from,Vec to,bool partial=false,
                            std::source_location caller=std::source_location::current());
    std::deque<Vec> joinTravelRoute(const Grid &terrain,const Grid &live,Vec from,Vec to);
    struct Component {Bytes cells;};
    const Component &connectionComponent(const Grid &,Vec from);
    bool prolongedWait() const;
    void noteTravelWait();
    void beginFrame(uint64_t game,uint64_t area,uint64_t geometry);
    void observeExplored(const RemoteTown &,const OnlineView &,std::optional<Vec> position);
    void traceDecision(nlohmann::json details);
    void clear();
    static constexpr int explorationClearance=6; // AI policy, in native subtiles.
    static constexpr int wallSearchRadius=explorationClearance+3;
    static float wallCost(int clearance) {
        return clearance<explorationClearance?2.f*(explorationClearance-clearance):float(clearance-explorationClearance);
    }
    int wallClearance(const Grid &terrain,Vec at);
    Vec explorationLane(const Grid &terrain,const Grid &collision,Vec from,Vec onRoute,Vec onward,bool walking);
    static constexpr int townClearance=4; // Preferred space at bends, never a collision requirement.
    Vec townWalkLane(const Grid &terrain,const Grid &collision,Vec from,Vec onRoute,Vec onward);
    // Nearby assigned NPCs can take over a relay through normal unit input.
    static bool directNpcApproach(const RemoteTown &,const OnlineView &,OnlineUnitKey,Vec from);
    std::deque<Vec> path(const Grid &,Vec from,Vec to,bool partial=false,
                        MovementCollisionRule rule={},bool native=false,
                        std::source_location caller=std::source_location::current());
    bool reachable(const Grid &,Vec from,Vec to,MovementCollisionRule rule={},bool native=false,
                   std::source_location caller=std::source_location::current());
    void reuseSegment() {++segmentReuses_;}
    nlohmann::json snapshot() const;
};
} // namespace d2x

#pragma once
#include "walk_contract.hpp"
#include <chrono>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <nlohmann/json.hpp>
#include "exploration_memory.hpp"
#include "client/remote_town.hpp"

namespace d2x::debug_ai {
struct NavigationState {
    using Clock=std::chrono::steady_clock;
    using Json=nlohmann::json;
    struct ExplorationReturn {
        uint16_t area{};bool returning{};Clock::time_point deadline;
        std::optional<uint16_t> waypoint;
    };
    std::optional<ExplorationReturn> explorationReturn;
    struct FrontierAttempt {
        Vec position;uint64_t geometryRevision{};Clock::time_point retryAt;
        bool reached{},retainAfterMove{};std::string outcome,reason;std::optional<OnlinePoint> relayGoal;
    };
    std::map<std::pair<uint16_t,uint16_t>,FrontierAttempt> frontierAttempts;
    enum class ExplorationFocus {Coverage,Units,Connection};
    struct ExperienceLandmark {
        uint8_t type{};uint16_t identity{};
        std::optional<int> townVariant;
        std::vector<OnlinePoint> positions;
    };
    struct AreaExperience {
        std::optional<std::array<int,4>> bounds;
        std::map<uint16_t,DebugExplorationMemory::Cell> travelDirections;
        std::vector<ExperienceLandmark> landmarks;
    };
    using NavigationExperience=std::map<uint16_t,AreaExperience>;
    NavigationExperience navigationExperience;
    struct FrontierRoute {
        uint16_t area{};OnlinePoint target;std::vector<OnlinePoint> path;
        uint64_t passage{},evidence{};size_t bestRemaining{~size_t{}};unsigned stalls{};
        Clock::time_point blockedSince{};std::optional<Clock::time_point> observationSince;
        ExplorationFocus focus=ExplorationFocus::Coverage;
    };
    std::optional<FrontierRoute> frontierRoute;
    std::set<std::tuple<uint16_t,uint8_t,uint16_t,uint16_t,uint16_t>> checkedHints;
    std::optional<std::array<uint16_t,48>> checkedHintQuestFlags;
    uint64_t mapCacheBuilds{},pathSearches{};
    std::optional<uint16_t> travelDestination;
    std::vector<int> travelRoute;
    std::set<int> travelVisitedAreas;
    std::optional<uint16_t> unavailableWaypointArea;
    std::map<std::pair<int,int>,unsigned> failedWalkEdges;
    std::optional<OnlinePoint> approachHint,approachFrontier;
    struct ApproachObservation {
        DebugWalkIntent owner;
        std::optional<OnlineUnitKey> unit;
        uint64_t assignment{},passage{},evidence{};
        OnlinePoint endpoint{};
        std::optional<Clock::time_point> observationSince;
        std::optional<OnlinePoint> relayRecovery;
        std::shared_ptr<const NavigationSnapshot> relayTerrain;
    } approachObservation;
    std::map<std::pair<uint16_t,uint16_t>,Clock::time_point> suppressedHints;
    DebugWalkIntent walkIntent;
    Json searchDiagnostics,approachDiagnostics,mobilityDiagnostics,travelDiagnostics;
    struct VaughtClues {
        struct Branch {
            std::vector<RemoteTown::NavigationWall> walls;
            Vec direction,observe,end; // Authored local tile coordinates, not a live spawn lookup.
        };
        bool prepared{};std::string unavailable;
        uint16_t level{},monster{};int width{},height{};
        std::vector<Branch> branches;
    } vaughtClues;
    struct VaughtSearch {
        uint64_t game{~uint64_t{}};uint32_t seed{},objectSeed{};int x{},y{};
        std::set<std::tuple<int,int,uint32_t>> walls;
        std::set<size_t> checkedViews;
        std::optional<size_t> branch,view;
        std::optional<OnlinePoint> aim;
        bool centerChecked{},conflict{},exhausted{};
        Clock::time_point retryAt{};
    } vaughtSearch;
    Json terrainClueDiagnostics;
    struct ColdPlainsSearch {
        uint64_t game{~uint64_t{}};uint32_t seed{};
        std::vector<OnlinePoint> views;size_t next{};
        bool prepared{},done{};Clock::time_point deadline{},settleUntil{};
    } coldPlainsSearch;
    std::set<std::pair<OnlineUnitKey,uint64_t>> operatedDoors;
    std::optional<std::pair<OnlineUnitKey,uint64_t>> pendingDoor;
    Clock::time_point doorDeadline{};
    enum class Passage {Door,CreatePortal,WorldEvent,Transfer};
    struct PassageWait {Clock::time_point retryAt{},deadline{};unsigned attempts{};};
    std::map<Passage,PassageWait> passageWaits;
    bool passageDefending{};
    struct WaypointActivation {OnlineUnitKey unit;uint64_t assignment{};uint16_t area{};bool menu{};};
    std::optional<WaypointActivation> pendingWaypoint;
    struct WaypointDeferral {OnlineUnitKey unit;uint64_t assignment{};Clock::time_point retryAt{};Vec position;};
    std::optional<WaypointDeferral> deferredWaypoint;
    std::optional<uint64_t> handledWaypointTimeout;
    std::set<uint16_t> requiredWaypointAreas;
    std::optional<uint16_t> waypointDepartureArea,nearbyWaypointArea;
    std::optional<OnlinePoint> missingWaypointAt;
    std::set<uint16_t> knownWaypointAreas; // Sole AI unlock memory; raw 63 remains the protocol snapshot.
    std::set<uint16_t> waypointTownArrivals;
    uint64_t waypointMemoryGame{},waypointMemoryRevision{};
    std::optional<uint64_t> waypointActivationGeneration;
    struct RememberedEnemy {
        OnlinePoint position;uint64_t assignment{};std::optional<uint16_t> identity;
        std::optional<uint16_t> superUnique{};bool elite{};
    };
    struct AreaPopulation {
        std::optional<uint32_t> seed;
        std::map<std::tuple<int,int,int>,uint64_t> rooms;
        std::map<std::tuple<int,int,int>,Clock::time_point> settlingRooms;
        std::set<std::tuple<int,int,int>> searchedRooms;
        std::map<OnlineUnitKey,RememberedEnemy> enemies;
        Clock::time_point readyAt{};
    };
    std::map<uint16_t,AreaPopulation> populationMemory;
    uint64_t populationMemoryGame{},populationMemoryRevision{};
    Clock::time_point waypointDeadline{};
    struct LocalPad {
        OnlineUnitKey unit;uint64_t assignment{};uint16_t area{},identity{};OnlinePoint source{},from{};
        uint64_t discontinuity{},requestRevision{};Clock::time_point deadline{};bool waiting{};
    };
    struct PadObservation {OnlinePoint destination;Clock::time_point used{};unsigned visits{};};
    std::optional<LocalPad> localPad;
    std::map<std::tuple<uint16_t,uint16_t,uint16_t,uint16_t>,PadObservation> padObservations;
    std::optional<OnlinePoint> padArrival;
    Clock::time_point padArrivedAt{};
    size_t padProgressCells{},padProgressTargets{};unsigned padTransfersWithoutDiscovery{};
    enum class TravelPhase {Route,WaypointOpen,WaypointWait,WalkExit,WarpExit,CrossWait} travelPhase{};
    std::optional<uint16_t> legDestination,waypointDestination;std::optional<OnlineUnitKey> legUnit;
    unsigned npcOpenRetries{};
    bool npcOpeningRecovery{};
    std::map<std::tuple<uint16_t,OnlineUnitKey,uint64_t>,unsigned> navigationRetries;
    struct TravelSubmission {
        OnlineUnitKey unit;uint64_t assignment{},game{},world{},request{},discontinuity{};uint16_t sourceArea{};
    };
    std::optional<TravelSubmission> submittedTravel;
    std::map<std::tuple<uint16_t,OnlineUnitKey,uint64_t>,unsigned> travelRetries;
    bool mobilityHold{},mobilityTeleport{};uint64_t mobilityDiscontinuity{};
    struct TeleportTimingObservation {
        std::optional<std::tuple<uint64_t,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,unsigned,int>> key;
        uint64_t revision{};
        std::optional<float> seconds;
    } teleportTiming;
    std::optional<OnlinePoint> mobilityTarget;Vec mobilityFrom;
    std::optional<OnlinePoint> mobilityGoal;uint16_t mobilityArea{};
    std::vector<Vec> mobilityRoute;float mobilityRouteProgress{},mobilityPendingProgress{},mobilityRouteLength{};
    Clock::time_point mobilityDeadline{},nextMobility{},lastSelection{},exitRetryAt{};
    std::optional<uint16_t> townExitArea;Clock::time_point townExitObserveUntil{};
    std::map<std::tuple<uint16_t,uint16_t,uint16_t,uint16_t,uint16_t>,unsigned> mobilityTrials;
    struct RouteCombatTask {
        std::map<OnlineUnitKey,uint64_t> targets;
        bool preferAreaAttack{};
    };
    struct RouteExecution {
        enum class Mode { TeleportTransit, WalkingPassage, CombatEntry } mode=Mode::TeleportTransit;
        enum class Phase { Relocate, Engage, AwaitCast, Resume } phase=Phase::Relocate;
        DebugWalkIntent intent;
        uint16_t area{};size_t step{};
        OnlinePoint landing{},position{};
        Vec segmentFrom,segmentTo;
        RouteCombatTask combat;
        uint64_t attackSequence{};
        bool relocated{},resumeRelocation{};
        Clock::time_point deadline;
    };
    std::optional<RouteExecution> routeExecution;
    std::optional<uint32_t> closeWaypoint;
    std::optional<uint32_t> observedWaypointMenu;
    Clock::time_point waypointMenuOpenedAt{};
    void resetStep();
    void suspend(Clock::duration);
};
} // namespace d2x::debug_ai

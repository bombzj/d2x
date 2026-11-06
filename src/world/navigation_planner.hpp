#pragma once
#include "navigation.hpp"
#include <atomic>
#include <array>
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <tuple>

namespace d2x {
inline constexpr size_t navigationCandidateLimit=65536;
class NavigationWorkScope {
    std::function<void()> checkpoint_;
    const std::function<void()> *previous_;
    public:
        explicit NavigationWorkScope(std::function<void()> checkpoint);
        ~NavigationWorkScope();
        NavigationWorkScope(const NavigationWorkScope &)=delete;
        NavigationWorkScope &operator=(const NavigationWorkScope &)=delete;
};
void navigationWorkCheckpoint();
struct NavigationDerivedCache {
    struct Block {
        std::array<uint16_t,36*36> masks;
        std::array<uint8_t,1024> edges{};
        std::array<uint16_t,1024> components{};
        uint64_t used{};
    };
    std::map<std::pair<int,int>,std::shared_ptr<const Block>> walking;
    uint64_t sequence{},hits{},builds{};
    size_t activeBytes{},peakActiveBytes{};
};
class NavigationCacheScope {
    NavigationDerivedCache *previous_;
  public:
    explicit NavigationCacheScope(NavigationDerivedCache &);
    ~NavigationCacheScope();
    NavigationCacheScope(const NavigationCacheScope &)=delete;
    NavigationCacheScope &operator=(const NavigationCacheScope &)=delete;
};
struct NavigationRoom {
    int x{},y{},width{},height{};
    std::vector<size_t> near;
    bool operator==(const NavigationRoom &) const = default;
};
struct NavigationPlanningRoom {
    int level{},x{},y{},width{},height{};
    bool loaded{};
    size_t exploredTiles{};
    std::vector<size_t> near;
    bool operator==(const NavigationPlanningRoom &) const = default;
};
struct NavigationRoomTree {
    std::vector<size_t> previous,depth;
    size_t root{};
    uint64_t geometry{};
};
struct NavigationPassageCache {
    struct Evidence {
        std::vector<uint16_t> masks;bool connected{};
        std::weak_ptr<const NavigationSnapshot> snapshot;
    };
    using Key=std::tuple<int,int,int,int,int,int,int,int,int,int>;
    std::map<Key,Evidence> entries;
    size_t bytes{};
};
NavigationRoomTree planRoomTree(const Grid &,const std::vector<NavigationPlanningRoom> &,size_t root,
    int level,int range,Vec origin,uint64_t geometry,NavigationPassageCache &,
    const std::shared_ptr<std::atomic<bool>> &cancel={});
enum class NavigationOutcome {Complete,Partial,NoRoute,Cancelled,ResourceLimited,NeedObservation,StartBlocked};
struct NavigationRoute {
    Vec origin;
    std::deque<Vec> points;
    NavigationOutcome outcome{NavigationOutcome::NoRoute};
};
bool navigationRouteValid(const Grid &,std::span<const NavigationRoom>,const NavigationRoute &,int range,bool restricted);
struct NavigationTarget {Vec point;float weight{1};size_t gain{1};float penalty{};};
struct NavigationCost {
    float walkUnit{1},teleportCast{},observation{8};
    bool seconds{};
    bool operator==(const NavigationCost &) const = default;
};
struct NavigationSelection {
    size_t candidate{};
    NavigationRoute route;
    bool found{};
    size_t expanded{};
    float travelCost{},score{};
    bool costInSeconds{};
};
enum class NavigationMobility {Walk,TownWalk,Teleport};
enum class NavigationObjective {Coverage,PerimeterConnection,InteriorEntrance,Units};
struct NavigationViewpointInput {
    Vec origin,from,halfExtent;
    std::vector<NavigationRoom> rooms;
    std::map<int,std::vector<int>> unseenBands;
    struct Exclusion {Vec point;float radius;};
    std::vector<Exclusion> exclusions;
    struct Boundary {int side,plane,start,end;};
    std::vector<Boundary> boundaries;
    std::optional<Vec> hub;
    float branchScale{};
    int teleportRange{};
    bool outdoor{};
    bool restricted{};
    NavigationMobility mobility{NavigationMobility::Walk};
    NavigationObjective objective{NavigationObjective::Coverage};
    NavigationCost cost;
};
struct NavigationViewpoint {Vec local;size_t gain{};float score{},weight{1},penalty{};};
struct NavigationExploration {
    NavigationSelection selection;
    std::vector<NavigationViewpoint> viewpoints;
};
struct NavigationBacktrackInput {
    Vec origin,from;
    std::vector<NavigationPlanningRoom> rooms;
    std::vector<NavigationRoom> routeRooms;
    std::vector<size_t> destinations;
    std::vector<float> priorities,weights;
    std::vector<NavigationViewpointInput::Exclusion> exclusions;
    size_t root{};
    int level{},range{};
    bool restricted{},clearing{};
    NavigationMobility mobility{NavigationMobility::Walk};
    NavigationCost cost;
};
struct NavigationBacktrack {
    NavigationSelection selection;
    NavigationRoomTree tree;
    std::vector<NavigationRoom> routeRooms;
    size_t destination{},candidates{};
};
NavigationBacktrack planBacktrack(const Grid &,const NavigationBacktrackInput &,NavigationPassageCache &,
    const std::shared_ptr<std::atomic<bool>> &cancel={});
NavigationExploration planExploration(const Grid &,const NavigationViewpointInput &,
    const std::shared_ptr<std::atomic<bool>> &cancel={});
std::vector<NavigationViewpoint> navigationViewpoints(const Grid &,const NavigationViewpointInput &,
    const std::shared_ptr<std::atomic<bool>> &cancel={},int spacing=5);
NavigationSelection planNavigation(const Grid &,std::span<const NavigationRoom>,Vec from,
    std::span<const NavigationTarget>,int teleportRange,bool restricted,
    const std::shared_ptr<std::atomic<bool>> &cancel={},bool partial=false,NavigationCost cost={});
}
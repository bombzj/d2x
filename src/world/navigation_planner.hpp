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
NavigationSelection planNavigation(const Grid &,std::span<const NavigationRoom>,Vec from,
    std::span<const NavigationTarget>,int teleportRange,bool restricted,
    const std::shared_ptr<std::atomic<bool>> &cancel={},bool partial=false,NavigationCost cost={});
}
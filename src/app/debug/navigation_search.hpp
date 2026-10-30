#pragma once
#include "world/navigation.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <source_location>
#include <string>
#include <tuple>

namespace d2x {
// Cooperative yield, not an unreachable route or an action failure. Only the
// debug decision boundary catches this; submitted requests keep their owner.
struct DebugSearchPending {};
class DebugNavigationSearch {
    using Clock=std::chrono::steady_clock;
    using Key=std::tuple<uint64_t,uint64_t,bool,int,int,uint16_t,int,bool,bool,float,float,float,float>;
    struct Result {std::deque<Vec> path;uint64_t used{};};
    std::map<Key,Result> results_;
    // Reachability only: a prefix of an already checked route is a proof,
    // not a claim that it is the shortest route to another candidate.
    std::map<Key,Key> reachable_;
    std::array<uint64_t,3> context_{};
    size_t points_{};
    uint64_t frames_{},queries_{},hits_{},proofHits_{},yields_{},resets_{},segmentReuses_{};
    unsigned frameQueries_{},frameHits_{};
    double frameMicros_{},totalMicros_{},maxMicros_{};
    std::string lastFile_,slowestFile_;
    uint_least32_t lastLine_{},slowestLine_{};
    Vec slowestFrom_{},slowestTo_{};
    Clock::time_point started_{};
    bool yielded_{};
  public:
    static constexpr unsigned queryBudget=8;
    static constexpr double microsecondBudget=2000;
    void beginFrame(uint64_t game,uint64_t area,uint64_t geometry);
    void clear();
    std::deque<Vec> path(const Grid &,Vec from,Vec to,bool partial=false,
                        MovementCollisionRule rule={},bool native=false,
                        std::source_location caller=std::source_location::current());
    bool reachable(const Grid &,Vec from,Vec to,MovementCollisionRule rule={},bool native=false,
                   std::source_location caller=std::source_location::current());
    void reuseSegment() {++segmentReuses_;}
    nlohmann::json snapshot() const;
};
} // namespace d2x

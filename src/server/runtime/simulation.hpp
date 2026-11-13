#pragma once
#include "contracts.hpp"
#include "frame_facts.hpp"
#include "system_metrics.hpp"
#include <array>
#include <string_view>

namespace d2x::server {
struct GameSystems;
// Fixed ordering only. No damage, population, inventory or quest rules here.
class Simulation {
    SystemSteps steps_{};
    SystemMetrics metrics_{};
    FrameFacts facts_;
    enum class Phase;
    Phase phase_;
    std::optional<uint64_t> frame_;
    uint64_t completedNativeEvent_{};
  public:
    Simulation();
    bool step(TickContext, GameSystems &);
    const SystemSteps &lastSteps() const { return steps_; }
    const SystemMetrics &metrics() const { return metrics_; }
    std::string_view pendingPhase() const;
};
}

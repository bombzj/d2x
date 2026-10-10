#pragma once
#include "contracts.hpp"
#include "events.hpp"
#include "system_catalog.hpp"
#include <array>

namespace d2x::server {
struct GameSystems;
using SystemSteps = std::array<std::optional<StepStatus>, size_t(SystemId::Count)>;
struct StageMetrics { uint64_t nanoseconds{}, calls{}, blocked{}; };
using SystemMetrics = std::array<StageMetrics,size_t(SystemId::Count)>;
// Fixed ordering only. No damage, population, inventory or quest rules here.
class Simulation {
    SystemSteps steps_{};
    SystemMetrics metrics_{};
    FrameFacts facts_;
  public:
    void step(TickContext, GameSystems &);
    const SystemSteps &lastSteps() const { return steps_; }
    const SystemMetrics &metrics() const { return metrics_; }
};
}

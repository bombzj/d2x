#pragma once
#include "server/player_identity.hpp"
#include "server/runtime/simulation.hpp"
#include "server/systems/spatial/footprint_metrics.hpp"
#include <cstdint>
#include <functional>
namespace d2x::hosting {
// Opt-in synchronous observations. Callbacks must not mutate the host or do IO.
struct StepTiming {
    GameHandle game;
    uint64_t before{},after{},nanoseconds{};
    server::SystemMetrics stages;
    server::spatial::FootprintMetrics footprints;
};
struct PumpTiming {
    uint64_t simulation{},preparation{},events{},publication{};
};
struct PerformanceObserver {
    std::function<void(const StepTiming &)> step;
    std::function<void(const PumpTiming &)> pump;
};
}

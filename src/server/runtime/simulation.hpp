#pragma once
#include "contracts.hpp"
#include "events.hpp"
#include "system_catalog.hpp"
#include <array>

namespace d2x::server {
struct GameSystems;
using SystemSteps = std::array<std::optional<StepStatus>, size_t(SystemId::Count)>;
// Fixed ordering only. No damage, population, inventory or quest rules here.
class Simulation {
    SystemSteps steps_{};
    FrameFacts facts_;
  public:
    void step(TickContext, GameSystems &);
    const SystemSteps &lastSteps() const { return steps_; }
};
}

#pragma once
#include "system_catalog.hpp"
#include <array>

namespace d2x::server {
using SystemSteps = std::array<std::optional<StepStatus>, size_t(SystemId::Count)>;
struct StageMetrics { uint64_t nanoseconds{}, calls{}, blocked{}; };
using SystemMetrics = std::array<StageMetrics,size_t(SystemId::Count)>;
}

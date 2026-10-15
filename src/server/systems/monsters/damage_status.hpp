#pragma once
#include "core/id.hpp"
#include <cstdint>
#include <optional>
namespace d2x::server::monsters {
struct DamageStatus {EntityId source;int64_t burn{};uint64_t burnFrames{};std::optional<bool> stunApplied;bool deferAction{},actionOnly{};};
}

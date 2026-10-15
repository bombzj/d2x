#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/effects/definition.hpp"
#include <cstdint>
#include <optional>
namespace d2x::server::monsters {
// Monster-owned control; effects prepare values but do not mutate AI live.
struct CurseControl {
    CurseAi ai{CurseAi::None};
    EntityId caster,target;
    uint64_t until{};
    std::optional<Vec> origin;
    int state{-1};
};
struct AttractionControl {EntityId target;uint64_t until{};};
struct AiResetTimer {uint64_t due{};bool continuous{};uint64_t identity{};};
}

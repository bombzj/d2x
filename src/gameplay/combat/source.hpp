#pragma once
#include "core/id.hpp"
#include "alignment.hpp"
#include <compare>
#include <cstdint>

namespace d2x {
struct CombatSourceBinding {
    EntityId actor, controller, credit;
    bool hostile{}, converted{};
    UnitAlignment alignment{UnitAlignment::Evil};
    uint64_t allegianceRevision{};
    auto operator<=>(const CombatSourceBinding &) const = default;
};
}
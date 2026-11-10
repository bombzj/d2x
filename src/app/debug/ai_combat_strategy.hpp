#pragma once
#include "contracts/online_world.hpp"
#include "core/math.hpp"
#include <cstddef>
#include <optional>
#include <vector>

namespace d2x {
struct Grid;
namespace debug_ai {
// Per-decision values, not a second owner of the engagement or world replica.
struct CombatSight {
    const Grid *grid{};
    Vec offset;
    uint16_t mask{};
    bool clear(Vec from,Vec to) const;
};
struct CombatSelection {
    std::vector<OnlineUnit> enemies;
    std::optional<OnlineUnit> chosen,elite;
    bool required{};
};
// Direct attacks retain the departure list and its observed-effect fallback.
// Support and buff strategies have separate completion/continuation rules.
struct AttackStrategy {
    std::optional<uint16_t> aura;
    uint16_t skill{};
    size_t row{},firstFallback{},groupEnemies{};
    std::optional<int> delay;
    bool delayed{};
};
struct StaticFieldOpportunity {
    bool boss{};
    unsigned rank{};
    std::optional<int> range;
    int floor{};
    bool floorKnown{};
};
} // namespace debug_ai
} // namespace d2x

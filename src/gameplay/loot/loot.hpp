#pragma once
#include "core/id.hpp"
#include "gameplay/loot/request.hpp"
#include "gameplay/loot/plan.hpp"
#include <array>
#include <cstddef>
#include <set>
#include <span>
#include <string_view>
#include <optional>
#include <string>
#include <vector>
#include <functional>

namespace d2x {
struct TreasureClass {
    std::string name;
    std::vector<std::string> codes;
    std::vector<int> weights;
    std::optional<int> picks, noDrop, group, level;
    std::array<std::optional<int>, 4> quality{};
};
struct TreasureSelection {
    std::string code;
    std::array<int, 4> quality{};
    std::vector<std::string> path;
};
struct TreasureRoll {
    std::string root;
    uint64_t randomState = 0;
    unsigned noDrops = 0;
    std::vector<TreasureSelection> selections;
};
using TreasureVisitor = std::function<bool(const TreasureSelection &, uint64_t &)>;
TreasureRoll selectTreasure(std::span<const TreasureClass> classes, std::string_view root, uint64_t seed,
                           int level = 0, const TreasureVisitor &visitor = {});
} // namespace d2x

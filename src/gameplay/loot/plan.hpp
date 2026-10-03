#pragma once
#include "core/math.hpp"
#include "gameplay/items/generation.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace d2x {
struct LootDrop {
    std::string code;
    unsigned quantity;
    Vec offset;
    unsigned level = 1;
    ItemGeneration generation;
};
struct LootPlan {
    uint64_t randomState = 0;
    unsigned noDrops = 0;
    std::string deferred;
    std::vector<LootDrop> drops;
};
} // namespace d2x

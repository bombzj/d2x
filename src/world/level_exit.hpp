#pragma once
#include "content/world/world_catalog.hpp"
#include "core/id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct LevelExit {
    struct BoundaryPassage {
        Vec departure, arrival;
    };
    int slot = 0, warp = 0;
    RegionId destination{};
    std::string name;
    Vec position, accessPoint, arrival;
    WarpRecord selection;
    EntityId stairObject;
    bool enabled = false;
    // A boundary is crossed by walking; a DS1 warp requires explicit activation.
    std::optional<MapRecipe::Boundary> boundary;
    std::vector<BoundaryPassage> passages;
};
} // namespace d2x

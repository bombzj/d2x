#pragma once
#include "core/math.hpp"
#include <array>
#include <string>

namespace d2x {
struct SkillOverlayVisual {
    int id = -1, frames = 0, trans = 5;
    float fps = 0;
    Vec offset;
    std::array<int, 4> heights{};
    bool preDraw = false;
    std::string art;
};
struct SkillImpactVisual {
    int missileId = -1;
    std::string art;
    float duration = 0;
};
} // namespace d2x

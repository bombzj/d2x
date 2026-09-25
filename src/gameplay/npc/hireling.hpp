#pragma once
#include "core/math.hpp"
#include <deque>
#include <string>
#include <cstdint>

namespace d2x {
// The quest reward is a character-owned hireling. Position and route are
// session state; the character save records identity, level, experience and life.
struct HirelingState {
    int sourceRow = -1;
    int classId = -1;
    std::string nameKey;
    int level = 0;
    float hp = 0;
    Vec pos, look{1, 0};
    std::deque<Vec> route;
    bool moving = false;
    float attackTimer = 0;
    uint64_t experience = 0;
    bool active() const { return sourceRow >= 0 && hp > 0; }
};
} // namespace d2x

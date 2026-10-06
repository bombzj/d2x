#pragma once
#include "gameplay/items/handle.hpp"
#include <vector>

namespace d2x {
struct ItemSkillGrant {
    ItemHandle item;
    int skill = -1, rank = 0;
};
} // namespace d2x

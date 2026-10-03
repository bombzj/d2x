#pragma once
#include "core/math.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include <vector>

namespace d2x {
struct MonsterSpawn {
    MonsterIdentity identity;
    MonsterKind kind = MonsterKind::Fallen;
    Vec position;
    std::vector<Vec> skillPositions = {};
};
} // namespace d2x

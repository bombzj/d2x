#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <variant>

namespace d2x {
struct SkillCast {
    EntityId actor;
    int skillId = -1;
    Vec position;
};
struct MissileImpact { int missileId; Vec position; };
struct MissileReleased { int missileId; };
// Ground damage source creation; presentation owns the independent falling sprite.
struct BlizzardShardCreated { int missileId; Vec position; };
struct SkillActivated { int skillId = -1; };
using SkillEvent = std::variant<SkillCast, MissileImpact, MissileReleased, BlizzardShardCreated, SkillActivated>;
} // namespace d2x

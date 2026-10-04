#pragma once
#include "core/id.hpp"
#include "gameplay/effects/definition.hpp"
#include <array>

namespace d2x {
struct AmazonMagicSpec {
    CombatStateDefinition state;
    int filter = 0, radius = 0, radiusPerLevel = 0;
    int frames = 0, framesPerLevel = 0, defenseReduction = 0;
    int slowPercent = 0, slowPerLevel = 0;
    std::array<int, 5> defensePerLevel{};
    int overlay = -1;
};
struct SkillCastSpec;
class ISkillWorld;
void releaseAmazonMagic(ISkillWorld &world, EntityId actor, const SkillCastSpec &skill);
} // namespace d2x

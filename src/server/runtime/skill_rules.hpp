#pragma once
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include "world/collision.hpp"
#include <map>
#include <string>
namespace d2x::server {
struct SkillDefinition { SkillSpec spec; bool allowedInTown{}; MissileCollisionRule collision; };
struct SkillRules {
    std::map<int, SkillDefinition> definitions;
    std::map<std::string, CastAnimationTiming, std::less<>> animations;
    std::map<int, std::pair<int, int>> fireMasteries;
};
}

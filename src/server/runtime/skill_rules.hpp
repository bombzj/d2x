#pragma once
#include "gameplay/skills/rule_spec.hpp"
#include "gameplay/skills/hydra_spec.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include "world/collision.hpp"
#include <array>
#include <map>
#include <string>
namespace d2x::server {
struct SkillDefinition { SkillRuleSpec spec; bool allowedInTown{}; MissileCollisionRule collision; };
struct SkillRules {
    std::optional<HydraSpec> hydra;
    std::map<int, SkillDefinition> definitions;
    std::map<std::string, CastAnimationTiming, std::less<>> animations;
    std::map<int, std::pair<int, int>> fireMasteries;
    std::map<int, std::pair<int, int>> lightningMasteries, coldMasteries;
    std::map<int, MissileCollisionRule> collisions;
    std::map<int, bool> returnFire, clientSend;
    std::array<int, 3> staticMinimum{}, coldDivisor{}, freezeDivisor{};
};
}

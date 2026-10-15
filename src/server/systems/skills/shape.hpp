#pragma once
#include "gameplay/actions/shape_profile.hpp"
#include "gameplay/actions/weapon_timing.hpp"
#include <optional>
#include <set>
#include <string_view>
namespace d2x::server {struct PlayerState;struct SkillRules;}
namespace d2x::server::skills {
const ShapeProfile *activeShape(const SkillRules &,const std::set<int> &);
bool shapeAllowsSkill(const SkillRules &,const std::set<int> &,int skill);
std::optional<WeaponAttackTiming> shapeActionTiming(const PlayerState &,const ShapeProfile &,std::string_view playerMode,bool skillUsesAttackRate=true);
}

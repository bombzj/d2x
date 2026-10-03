#pragma once
#include "gameplay/character/allocation.hpp"
#include <variant>

namespace d2x {
struct AllocateAttribute { Attribute attribute = Attribute::Strength; };
struct AllocateSkill { int id = -1; };
struct BindSkillHotkey { unsigned index = 0; int skill = -2; bool right = true; };
struct SelectMouseSkill { int skill = -1; bool right = true; };
using CharacterIntent = std::variant<AllocateAttribute, AllocateSkill, BindSkillHotkey, SelectMouseSkill>;
} // namespace d2x

#pragma once
#include "skill_data.hpp"
#include "resources/archive.hpp"
#include <span>

namespace d2x {
void loadSkillAnimations(SkillCatalog &catalog, const DataTable &skills, const DataTable &weapons,
                         std::span<const CharacterDefinition> characters, Archives &archives);
}

#pragma once
#include "skill_data.hpp"
#include "resources/archive.hpp"
#include <span>

namespace d2x {
void loadPlayerSkillAnimations(SkillCatalog &, const DataTable &weapons,
                              std::span<const CharacterDefinition>, Archives &);
HydraSpec loadHydraHeadAnimations(const DataTable &skills,const DataTable &monsters,
                                 const DataTable &extras,Archives &,std::string_view firstHead);
void loadSkillAnimations(SkillCatalog &catalog, const DataTable &skills, const DataTable &weapons,
                         std::span<const CharacterDefinition> characters, Archives &archives);
}

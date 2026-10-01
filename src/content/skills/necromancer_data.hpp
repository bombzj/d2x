#pragma once
#include "skill_data.hpp"
#include "resources/archive.hpp"
namespace d2x {
void loadNecromancerSummons(SkillCatalog &catalog, const DataTable &skills, const DataTable &monstats,
                           const DataTable &monstats2, const DataTable &monlvl, const DataTable &sounds,
                           Archives &archives);
}

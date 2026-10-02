#pragma once
#include "skill_data.hpp"
#include "state_data.hpp"
#include "resources/archive.hpp"
namespace d2x {
void loadNecromancerCurses(SkillCatalog &catalog, const DataTable &skills, const DataTable &overlays, const DataTable &sounds,
                          const CombatStateCatalog &states, Archives &archives);
void loadNecromancerSummons(SkillCatalog &catalog, const DataTable &skills, const DataTable &monstats,
                           const DataTable &monstats2, const DataTable &monlvl, const DataTable &sounds,
                           Archives &archives);
}

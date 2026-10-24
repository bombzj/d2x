#include "town_data.hpp"
#include "skill_data.hpp"
#include "aura_data.hpp"
#include "curse_data.hpp"
#include "summon_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/formula_data.hpp"
#include "content/items/cube_data.hpp"
#include "content/skills/druid_data.hpp"
#include "content/skills/assassin_data.hpp"
#include "resources/archive.hpp"
namespace d2x {
void preparePd2TownData(ClassicData &data, Archives &archives) {
    data.pd2Host=true;
    data.profile="pd2-town-host-v1";
    data.nativeFormulas=prepareSkillFormulaCatalog(data, archives.read("data/global/excel/missiles.bin"));
    preparePd2SkillData(data,archives);
    preparePd2Auras(data);
    preparePd2Curses(data);
    preparePd2NecromancerSummons(data,archives);
    loadDruidSkills(data,archives);
    loadAssassinSkills(data,archives);
    loadLodTreasureData(data);
    loadCubeData(data);
}
}

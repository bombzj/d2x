#include "town_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/formula_data.hpp"
#include "content/items/cube_data.hpp"
#include "resources/archive.hpp"
namespace d2x {
void preparePd2TownData(ClassicData &data, Archives &archives) {
    data.pd2Host=true;
    data.profile="pd2-town-host-v1";
    data.nativeFormulas=prepareSkillFormulaCatalog(data, archives.read("data/global/excel/missiles.bin"));
    loadCubeData(data);
}
}

#include "town_host.hpp"
#include "content/world/world_catalog.hpp"
#include "gameplay/monsters/region_generation.hpp"
#include <stdexcept>
namespace d2x::hosting::pd2 {
void validateArea(const LevelRecord &area) {
    if(area.town || (area.id>0 && area.id<=136)) return;
    // Extended layouts use the actual Levels/LvlPrest binding, native preset
    // selection, authored DS1 units, warp slots and DT1 collision. Admission
    // does not grant any item, event controller or boss program.
    if(area.id>136 && area.id<=255 && area.generation==GenerationKind::Preset &&
        area.act>=0 && area.act<5 && area.width>0 && area.height>0) return;
    throw std::runtime_error("Unsupported PD2 additional level layout");
}
std::shared_ptr<const MonsterRegionRules> prepareTownRegionRules(Archives &archives) {
    MonsterRegionRules rules;
    const auto world=sharedWorldCatalog(archives,0);
    for(const auto &[id,level]:world->levels()) if(level.town) {
        if(level.population.types || !level.population.normal.empty() || !level.population.nightmareHell.empty())
            throw std::runtime_error("PD2 town has unsupported hostile population rules");
        rules.levels.emplace(id,MonsterRegionLevel{});
    }
    return std::make_shared<const MonsterRegionRules>(std::move(rules));
}
}

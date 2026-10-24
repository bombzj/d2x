#include "town_host.hpp"
#include "content/world/world_catalog.hpp"
#include "gameplay/monsters/region_generation.hpp"
#include <stdexcept>
namespace d2x::hosting::pd2 {
void validateTownArea(const LevelRecord &area) {
    // The original five-act map generators are shared. PD2's additional map
    // levels require separate map/event programs before they may be admitted.
    if(area.town || (area.id>0 && area.id<=136)) return;
    throw std::runtime_error("PD2 hosting does not yet implement this additional map program");
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

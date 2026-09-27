#include "core/random.hpp"
#include "chest.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace d2x {
namespace {
uint32_t roll(uint64_t &seed, uint32_t bound) {
    return limitedRandom(seed, bound);
}
int number(const std::map<std::string, std::string> &row, const char *key) {
    const auto &value = row.at(key);
    return value.empty() ? 0 : std::stoi(value);
}
}
int resolveAct1ChestPreset(int objectClass, int levelId, uint64_t &seed) {
    if (objectClass != 580 && objectClass != 581) return objectClass;
    // Objects.cpp::OBJECTS_SpawnPresetChest, Act I. These are identities, not tokens.
    constexpr std::array classes{5, 6, 139, 140, 141, 144, 176, 177, 198, 240, 241, 242, 243};
    const int chosen = classes[roll(seed, unsigned(classes.size()))];
    // The tower placeholder belongs to the Countess quest, not a normal loot chest.
    return objectClass == 580 && levelId == 25 ? 371 : chosen;
}
void initializeChests(Region &region, const WorldCatalog &catalog, const Table &objects) {
    const auto area = catalog.levels().find(int(region.definition.id));
    for (auto &object : region.objects) {
        if (object.operateFn != 4) continue;
        const auto row = std::find_if(objects.begin(), objects.end(), [&](const auto &record) {
            return number(record, "Id") == object.objectClass;
        });
        if (row == objects.end()) throw std::runtime_error("Missing MPQ chest record");
        const int init = number(*row, "InitFn");
        if (init != 3 && init != 57) {
            object.interaction = Interaction::None;
            continue;
        }
        if (area == catalog.levels().end() || !area->second.objectLevel || *area->second.objectLevel < 0)
            throw std::runtime_error("Chest initialization requires Levels.MonLvl1");
        const int level = *area->second.objectLevel;
        ChestState chest;
        chest.sparkly = init == 57 || (object.chest && object.chest->sparkly);
        chest.lockable = number(*row, "Lockable") != 0;
        resetChestRandom(chest, level, region.objectSeed);
        object.chest = chest;
        if (object.animationMode != 0) object.interaction = Interaction::None;
    }
}
} // namespace d2x

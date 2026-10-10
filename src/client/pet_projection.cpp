#include "pet_projection.hpp"
#include "content/classic_data.hpp"
#include "contracts/online_world.hpp"
#include <utility>

namespace d2x {
std::vector<PetPortraitView> projectPetPortraits(const ClassicData &data, const OnlineWorldView &world, uint32_t owner) {
    std::vector<PetPortraitView> result;
    for (const auto &[type, definition] : data.petPortraits) {
        if ((!definition.hireling && !definition.iconType) || definition.baseArt.empty()) continue;
        const OnlinePet *first = nullptr;
        uint32_t firstId = 0;
        int count = 0;
        for (const auto &[id, pet] : world.pets) {
            if (pet.owner != owner || pet.type != type) continue;
            if (definition.hireling && (world.deadHirelingName || !world.hireling || world.hireling->id != id)) continue;
            if (!first) { first = &pet; firstId = id; }
            ++count;
        }
        if (!first) continue;
        PetPortraitView entry;
        entry.hireling = definition.hireling;
        entry.count = count;
        entry.showCount = definition.iconType == 2;
        entry.art = definition.art(first->monsterClass);
        entry.name = definition.name;
        if (count == 1) {
            entry.single = EntityId{(uint64_t{1} << 32) + firstId + 1};
            if (definition.drawLife) entry.lifePercentage = first->lifePercentage;
        }
        if (entry.hireling) {
            for (const auto &[key, index] : data.hirelingNameIds) if (index == int(world.hireling->name)) {
                const auto name = data.hirelingStrings.find(key);
                if (name != data.hirelingStrings.end()) entry.name = name->second;
                break;
            }
            // Native PetUI draws the hireable before the PetType icon loop.
            result.insert(result.begin(), std::move(entry));
        } else result.push_back(std::move(entry));
    }
    return result;
}
} // namespace d2x

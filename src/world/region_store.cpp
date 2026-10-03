#include "world/region_store.hpp"
#include "world/region_visibility.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace d2x {
void RegionStore::initialize(EntityIds &ids, const WorldPlan &plan, const MonsterCatalog &monsters,
                             const WorldCatalog &catalog, uint32_t mapSeed, uint32_t objectSeed) {
    if (!regions_.empty()) throw std::logic_error("Region storage is already initialized");
    auto random = initialRandom(mapSeed);
    levelSeed_ = rollRandom(random);
    regions_ = loadRegions(archives_, ids, plan.regions, monsters, catalog, mapSeed, objectSeed, true);
    entries_ = plan.entries;
}
int RegionStore::index(RegionId id) const {
    const auto found = std::find_if(regions_.begin(), regions_.end(),
        [id](const Region &value) { return value.definition.id == id; });
    return found == regions_.end() ? -1 : int(found - regions_.begin());
}
const Region *RegionStore::find(RegionId id) const {
    const auto slot = index(id);
    return slot < 0 ? nullptr : &regions_[size_t(slot)];
}
bool RegionStore::ensure(RegionId id, bool neighbours, EntityIds &ids, const MonsterCatalog &monsters,
                         const WorldCatalog &catalog, const std::function<void(Region &)> &loaded) {
    const auto slot = index(id);
    if (slot < 0) return false;
    bool changed = false;
    auto load = [&](Region &region) {
        if (region.loaded) return;
        loadRegion(archives_, ids, region, tiles_, monsters, catalog, levelSeed_);
        changed = true;
        loaded(region); // Preserve host initialization/random consumption before the next slot.
    };
    auto &origin = regions_[size_t(slot)];
    load(origin);
    if (neighbours)
        for (const auto &boundary : origin.recipe.boundaries)
            for (auto &candidate : regions_)
                if (int(candidate.definition.id) == boundary.destination) load(candidate);
    if (changed) linkLevelExits(regions_, catalog);
    return changed;
}
std::vector<std::pair<int, Vec>> RegionStore::sceneRegions(RegionId id, bool recursive) const {
    return connectedRegionSlots(regions_, id, recursive);
}
std::vector<std::pair<int, Vec>> connectedRegionSlots(std::span<const Region> regions, RegionId id, bool recursive) {
    const auto found = std::find_if(regions.begin(), regions.end(),
        [id](const auto &region) { return region.definition.id == id; });
    const auto slot = found == regions.end() ? -1 : int(found - regions.begin());
    if (slot < 0) return {};
    std::vector<std::pair<int, Vec>> result{{slot, {}}};
    std::set<int> included{slot};
    const auto &origin = regions[size_t(slot)].recipe;
    for (size_t next = 0; next < result.size(); ++next) {
        const auto &recipe = regions[size_t(result[next].first)].recipe;
        for (int index = 0; index < int(regions.size()); ++index) {
            const auto &candidate = regions[size_t(index)];
            if (included.contains(index) || !candidate.loaded ||
                std::none_of(recipe.boundaries.begin(), recipe.boundaries.end(), [&](const auto &boundary) {
                    return boundary.destination == int(candidate.definition.id);
                })) continue;
            included.insert(index);
            result.push_back({index, {float((candidate.recipe.worldX - origin.worldX) * 5),
                                     float((candidate.recipe.worldY - origin.worldY) * 5)}});
        }
        if (!recursive) break;
    }
    return result;
}
bool RegionStore::visibleRoom(RegionId originId, Vec observerPosition, RegionId targetId, Vec position) const {
    const auto *origin = find(originId), *candidate = find(targetId);
    if (!origin || !candidate || !origin->loaded || !candidate->loaded) return false;
    if (originId == targetId) return origin->map.activation.nearby(observerPosition, position);
    if (std::none_of(origin->recipe.boundaries.begin(), origin->recipe.boundaries.end(), [&](const auto &boundary) {
        return boundary.destination == int(targetId);
    })) return false;
    const auto *observer = origin->map.activation.room(observerPosition);
    const auto *target = candidate->map.activation.room(position);
    if (!observer || !target) return false;
    const int dx = (candidate->recipe.worldX - origin->recipe.worldX) * 5;
    const int dy = (candidate->recipe.worldY - origin->recipe.worldY) * 5;
    return observer->x <= target->x + dx + target->width && target->x + dx <= observer->x + observer->width &&
           observer->y <= target->y + dy + target->height && target->y + dy <= observer->y + observer->height;
}
} // namespace d2x

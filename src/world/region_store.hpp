#pragma once
#include "world/region.hpp"
#include <functional>
#include <span>

namespace d2x {
// Fixed slots for one game. Loading fills a slot; it never relocates Maps/Grids.
// This owns authored resources and object/collision state, not observer activity.
class RegionStore {
    Archives &archives_;
    TileLibraryCache tiles_;
    uint32_t levelSeed_ = 0;
    std::vector<Region> regions_;
    std::vector<WorldEntry> entries_;
  public:
    explicit RegionStore(Archives &archives) : archives_(archives), tiles_(archives) {}
    RegionStore(const RegionStore &) = delete;
    RegionStore &operator=(const RegionStore &) = delete;
    void initialize(EntityIds &ids, const WorldPlan &plan, const MonsterCatalog &monsters,
                    const WorldCatalog &catalog, uint32_t mapSeed, uint32_t objectSeed);
    bool ensure(RegionId id, bool neighbours, EntityIds &ids, const MonsterCatalog &monsters,
                const WorldCatalog &catalog, const std::function<void(Region &)> &loaded);
    int index(RegionId id) const;
    const Region *find(RegionId id) const;
    const std::vector<WorldEntry> &entries() const { return entries_; }
    // Authority adapters can mutate slots, but cannot resize or relocate storage.
    std::span<Region> regions() { return regions_; }
    const std::vector<Region> &regions() const { return regions_; }
    Region &at(size_t slot) { return regions_.at(slot); }
    const Region &at(size_t slot) const { return regions_.at(slot); }
    std::vector<std::pair<int, Vec>> sceneRegions(RegionId origin, bool recursive = false) const;
    bool visibleRoom(RegionId origin, Vec observer, RegionId target, Vec position) const;
};
} // namespace d2x

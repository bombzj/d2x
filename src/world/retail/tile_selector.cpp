#include "tile_selector.hpp"
#include <limits>
#include <stdexcept>

// D2MOO GetTileCache; native per-file push-front chain order cross-checked
// with libd2 tilegen (1.14d). Neither source certifies the 1.13c runtime.
// Attribution: docs/resources/THIRD_PARTY.md.
namespace d2x {
RetailTileSelector::RetailTileSelector(TileLibraryCache &cache, const WorldCatalog &catalog,
                                     int levelType, uint32_t mask) {
    for (const auto &path : catalog.terrainLibraries(levelType, mask)) {
        auto library = cache.load(path);
        libraries_.push_back(library);
        paths_.push_back(normalize(path));
        // Slot order is forward; same-identity records within one DT1 are reverse.
        for (auto tile = library->rbegin(); tile != library->rend(); ++tile) {
            auto &matches = lookup_[tile->key()];
            if (matches.size() < 40) matches.push_back(&*tile);
        }
    }
}
std::pair<std::string_view, size_t> RetailTileSelector::identity(const Tile &tile) const {
    const auto address = reinterpret_cast<uintptr_t>(&tile);
    for (size_t i = 0; i < libraries_.size(); ++i) {
        const auto &library = *libraries_[i];
        const auto first = reinterpret_cast<uintptr_t>(library.data());
        if (address >= first && address - first < library.size() * sizeof(Tile) &&
            (address - first) % sizeof(Tile) == 0)
            return {paths_[i], (address - first) / sizeof(Tile)};
    }
    throw std::runtime_error("Materialized DT1 tile has no retained source library");
}
const Tile &RetailTileSelector::pick(int orientation, uint32_t packed, Seed &random) const {
    const uint32_t key = (((packed >> 20) & 63) << 16) | (((packed >> 8) & 255) << 8) |
                         uint32_t(orientation);
    auto found = lookup_.find(key);
    if (found == lookup_.end() || found->second.empty()) {
        // The original fallback is the first real Warp.dt1 (10,0,0), no draw.
        // Missing fallback is an asset error; never synthesize a collision tile.
        found = lookup_.find(10);
        if (found == lookup_.end() || found->second.empty())
            throw std::runtime_error("Native DT1 identity and fallback are missing");
        return *found->second.front();
    }
    int total = 0;
    for (const auto *tile : found->second) {
        if (tile->rarity < 0 || tile->rarity > std::numeric_limits<int>::max() - total)
            throw std::runtime_error("Unsupported native DT1 rarity sum");
        total += tile->rarity;
    }
    int roll = random.below(total) + 1;
    if (!total) return *found->second.front();
    // A single entry still consumes the draw, even though the choice is fixed.
    for (const auto *tile : found->second) {
        roll -= tile->rarity;
        if (roll <= 0) return *tile;
    }
    throw std::runtime_error("Native DT1 weighted lookup has no selected entry");
}
std::span<const Tile *const> RetailTileSelector::matching(int orientation, uint32_t packed) const {
    const uint32_t key = (((packed >> 20) & 63) << 16) | (((packed >> 8) & 255) << 8) |
        uint32_t(orientation);
    const auto found = lookup_.find(key);
    return found == lookup_.end() ? std::span<const Tile *const>{} : found->second;
}
} // namespace d2x

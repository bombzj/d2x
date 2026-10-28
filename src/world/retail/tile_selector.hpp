#pragma once
#include "world/map.hpp"
#include "world/generation_seed.hpp"
#include <span>

namespace d2x {
// Native room library order and forty-entry weighted lookup. Libraries retain
// selected Tile pointers; no renderer, navigation or gameplay ownership.
class RetailTileSelector {
    std::vector<std::shared_ptr<const std::vector<Tile>>> libraries_;
    std::vector<std::string> paths_;
    std::map<uint32_t, std::vector<const Tile *>> lookup_;
    RetailTileSelector() = default;
    void index();
  public:
    RetailTileSelector(TileLibraryCache &, const WorldCatalog &, int levelType, uint32_t mask);
    const Tile &pick(int orientation, uint32_t packed, Seed &) const;
    std::span<const Tile *const> matching(int orientation, uint32_t packed) const;
    const auto &libraries() const { return libraries_; }
    std::pair<std::string_view, size_t> identity(const Tile &) const;
    // Retain record order/rarity/collision, without carrying decoded images
    // into an authoritative room generation session.
    std::shared_ptr<RetailTileSelector> collisionOnly(Archives &) const;
    const Tile &record(std::string_view path, size_t index) const;
};
} // namespace d2x

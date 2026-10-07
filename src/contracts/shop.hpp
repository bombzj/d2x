#pragma once
#include "core/id.hpp"
#include "gameplay/items/quality.hpp"
#include "gameplay/items/display.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct ShopOfferView {
    uint32_t slot = 0;
    int storePage = -1, width = 0, height = 0;
    std::string definition, artKey, name;
    ItemQuality quality = ItemQuality::Normal;
    unsigned price = 0;
    bool priceKnown = false;
    std::vector<ItemTextLine> tooltip; // Populated by inspection, not for every shelf item.
};
// Visible stock and current display prices only; no rolls, seed or hidden gamble outcome.
struct ShopView {
    uint64_t revision = 0;
    EntityId actor, npc;
    bool gamble = false, available = false, repairAvailable = false;
    bool pricesKnown = false;
    unsigned bankGold = 0;
    std::optional<unsigned> repairAllPrice;
    std::array<std::string, 4> tabLabels;
    std::vector<ShopOfferView> offers;
};
} // namespace d2x

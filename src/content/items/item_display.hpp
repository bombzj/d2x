#pragma once
#include "gameplay/items/display.hpp"
#include <array>

namespace d2x {
struct ClassicData;
class ItemCatalog;
struct ItemInstance;
struct ItemDisplayContext {
    int level = 1, strength = 0, dexterity = 0;
    unsigned maximumDurability = 0;
    std::array<int, 5> cainStones{};
};
std::string displayItemName(const ClassicData &content, const ItemCatalog &catalog, const ItemInstance &item);
ItemDisplay describeInventoryItem(const ClassicData &content, const ItemCatalog &catalog,
                                 const ItemInstance &item, const ItemDisplayContext &context);
} // namespace d2x

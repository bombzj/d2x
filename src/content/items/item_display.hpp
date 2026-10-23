#pragma once
#include "gameplay/items/display.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace d2x {
struct ClassicData;
class ItemCatalog;
struct ItemInstance;
class ClassicStrings;
// Server-indexed names/art only; no generation eligibility or property rolls.
void loadRemoteItemDisplayData(ClassicData &content, const ClassicStrings &strings);
struct ItemDisplayContext {
    int level = 1, strength = 0, dexterity = 0;
    unsigned maximumDurability = 0;
    std::array<int, 5> cainStones{};
    std::string_view characterClass;
    std::span<const int32_t> equippedSetRows;
    bool equipmentKnown = false;
    bool itemEquipped = false;
    std::optional<int> ownerMissingHp;
};
std::string displayItemName(const ClassicData &content, const ItemCatalog &catalog, const ItemInstance &item);
ItemDisplay describeInventoryItem(const ClassicData &content, const ItemCatalog &catalog,
                                 const ItemInstance &item, const ItemDisplayContext &context);
} // namespace d2x

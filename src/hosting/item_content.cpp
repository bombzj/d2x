#include "item_content.hpp"
#include "content/items/item_properties.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
ItemInstance prepareItem(const ClassicData &data, const LootDrop &drop, uint64_t &random, int difficulty) {
    const auto *definition = data.items.find(drop.code);
    if (!definition || !drop.quantity || drop.quantity > definition->maxStack || drop.level < 1 || drop.level > 99)
        throw std::runtime_error("Invalid prepared item generation");
    // Ported from master's InventoryService::createItem. Placement, ownership
    // and entity allocation deliberately remain in the authority.
    const auto &generation = drop.generation;
    ItemInstance item;
    item.definition = drop.code; item.quantity = drop.quantity; item.level = drop.level;
    item.charges = definition->bookInitialCharges; item.quality = generation.quality;
    item.identified = item.quality != ItemQuality::Magic && item.quality != ItemQuality::Rare &&
        item.quality != ItemQuality::Set && item.quality != ItemQuality::Unique;
    item.specialRow = generation.specialRow; item.requiredLevel = generation.requiredLevel;
    item.gradeRow = generation.gradeRow; item.rarePrefixRow = generation.rarePrefixRow;
    item.rareSuffixRow = generation.rareSuffixRow; item.propertyRolls = generation.propertyRolls; item.affixes = generation.affixes;
    auto identified = item; identified.identified = true;
    const auto stats = resolveItemStats(data, identified, int(item.level));
    const auto property = [&](std::string_view name) { int value = 0; for (const auto &stat : stats) if (stat.name == name) value += int(stat.value); return value; };
    item.durability = itemMaximumDurability(data, identified, stats);
    item.nativeSeed = rollRandom(random);
    auto local = initialRandom(item.nativeSeed);
    if (!definition->inventoryIcons.empty()) {
        item.nativeHasGraphic = true; item.nativeGraphic = limitedRandom(local, unsigned(definition->inventoryIcons.size()));
    }
    if (definition->family == ItemFamily::Armor) {
        const auto minimum = definition->base.minDefense, maximum = definition->base.maxDefense;
        if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum) throw std::runtime_error("Missing armor defense");
        item.defense = *minimum + int(limitedRandom(local, unsigned(*maximum - *minimum + 1)));
        if (item.quality == ItemQuality::Inferior) item.defense = std::max(1, item.defense * 75 / 100);
        if (property("item_armor_percent")) item.defense = *maximum + 1;
    }
    const unsigned limit = unsigned(std::max(0, std::min({definition->base.sockets.value_or(0),
        definition->base.socketsByLevel[item.level <= 25 ? 0 : item.level <= 40 ? 1 : 2], definition->width * definition->height, 6})));
    if (definition->maxStack == 1 && limit) {
        if (const auto sockets = property("item_numsockets"); sockets > 0) item.sockets = std::min(limit, unsigned(sockets));
        else if (generation.rollNaturalSockets && (item.quality == ItemQuality::Normal || item.quality == ItemQuality::Superior) && limitedRandom(local, 100) < 33) {
            constexpr unsigned caps[]{3, 4, 6};
            item.sockets = item.nativeSeed % std::min(limit, caps[std::clamp(difficulty, 0, 2)]) + 1;
        }
    }
    if (item.sockets) item.nativeFlags |= 0x800;
    item.nativeQuestDifficulty = unsigned(difficulty);
    return item;
}
}

#include "content/items/item_display.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_properties.hpp"
#include "gameplay/items/state.hpp"
#include <algorithm>

namespace d2x {
namespace {
const SpecialItemRecord *specialItem(const ClassicData &content, const ItemInstance &item) {
    if (item.specialRow < 0)
        return nullptr;
    const auto &records = item.quality == ItemQuality::Unique ? content.uniqueItems
                                                                : content.setItems;
    auto found = std::find_if(records.begin(), records.end(),
                              [&](const auto &record) { return int32_t(record.row) == item.specialRow; });
    return found == records.end() ? nullptr : &*found;
}
} // namespace
static std::string baseDisplayItemName(const ClassicData &content, const ItemCatalog &catalog, const ItemInstance &item) {
    auto localized = [&](const std::string &key) {
        const auto &strings = content.itemStrings;
        auto found = strings.find(key);
        return found == strings.end() ? key : found->second;
    };
    if (!item.identified) {
        const auto *definition = catalog.find(item.definition);
        return definition ? definition->name : item.definition;
    }
    if (item.runewordRow >= 0)
        for (const auto &record : content.runewords)
            if (record.row == item.runewordRow) return record.name;
    if (auto special = specialItem(content, item))
        return localized(special->name);
    const auto *definition = catalog.find(item.definition);
    std::string name = definition ? definition->name : item.definition;
    if (item.quality == ItemQuality::Superior)
        return "Superior " + name;
    if (item.quality == ItemQuality::Inferior) {
        auto found = std::find_if(content.inferiorGrades.begin(),
                                  content.inferiorGrades.end(),
                                  [&](const auto &record) { return int32_t(record.row) == item.gradeRow; });
        if (found != content.inferiorGrades.end())
            return localized(found->name) + " " + name;
    }
    if ((item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted)) {
        auto rareName = [&](const auto &records, int32_t row) -> std::string {
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == row; });
            return found == records.end() ? std::string{} : localized(found->name);
        };
        auto prefix = rareName(content.rarePrefixes, item.rarePrefixRow);
        auto suffix = rareName(content.rareSuffixes, item.rareSuffixRow);
        if (!prefix.empty() && !suffix.empty())
            return prefix + " " + suffix;
    }
    if (item.quality == ItemQuality::Magic)
        for (const auto &affix : item.affixes) {
            const auto &records = affix.prefix ? content.magicPrefixes
                                                : content.magicSuffixes;
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == affix.row; });
            if (found != records.end())
                name = affix.prefix ? localized(found->name) + " " + name : name + " " + localized(found->name);
        }
    return name;
}
std::string displayItemName(const ClassicData &content, const ItemCatalog &catalog, const ItemInstance &item) {
    auto name = baseDisplayItemName(content, catalog, item);
    return item.personalizedName.empty() ? name : item.personalizedName + "'s " + name;
}
ItemDisplay describeInventoryItem(const ClassicData &content, const ItemCatalog &catalog,
                                 const ItemInstance &item, const ItemDisplayContext &context) {
    const auto &definition = *catalog.find(item.definition);
    ItemDisplay display;
    display.name = displayItemName(content, catalog, item);
    const bool plain = item.quality == ItemQuality::Normal || item.quality == ItemQuality::Superior || item.quality == ItemQuality::Inferior;
    display.tooltip.push_back({display.name, item.identified && item.runewordRow >= 0 ? ItemTextTone::RunewordName :
        plain && (item.sockets || (item.nativeFlags & 0x400000u)) ? ItemTextTone::SocketedName : ItemTextTone::Name});
    if (item.runewordRow >= 0 && item.identified) display.tooltip.push_back({definition.name, ItemTextTone::RunewordName});
    if (item.sockets) display.tooltip.push_back({content.itemStrings.at("Socketable") + " (" + std::to_string(item.sockets) + ")", ItemTextTone::Property});
    for (const auto &child : item.socketedItems)
        display.tooltip.push_back({displayItemName(content, catalog, child), ItemTextTone::Property});
    auto line = [&](std::string value, ItemTextTone tone = ItemTextTone::Normal) {
        if (!value.empty()) display.tooltip.push_back({std::move(value), tone});
    };
    if (item.definition == "toa")
        if (const auto text = content.itemStrings.find("toa"); text != content.itemStrings.end())
            if (const auto separator = text->second.rfind('}'); separator != std::string::npos)
                line(text->second.substr(0, separator));
    {
        if ((item.quality == ItemQuality::Unique || item.quality == ItemQuality::Set ||
             (item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted)) && item.identified) line(definition.name, ItemTextTone::Name);
        const auto stats = resolveItemStats(content, item, context.level);
        auto sum = [&](const char *name) {
            int result = 0;
            for (const auto &stat : stats) if (stat.effect == name) result += stat.value;
            return result;
        };
        const auto &base = definition.base;
        auto damage = [&](const char *label, std::optional<int> low, std::optional<int> high, bool thrown = false) {
            if (!low || !high) return;
            if (item.nativeFlags & 0x400000u) { low = *low * 3 / 2; high = *high * 3 / 2; }
            if (item.quality == ItemQuality::Inferior) {
                low = std::max(thrown ? 2 : 1, *low * 75 / 100);
                high = std::max(thrown ? 1 : 2, *high * 75 / 100);
            }
            const int minimum = std::max(1, *low * (100 + sum("item_mindamage_percent")) / 100 + sum("mindamage"));
            const int maximum = std::max(minimum + 1, *high * (100 + sum("item_maxdamage_percent")) / 100 + sum("maxdamage"));
            line(std::string(label) + std::to_string(minimum) + " - " + std::to_string(maximum));
        };
        if (definition.family == ItemFamily::Weapon) {
            if (definition.equipment.throwable) damage("Throw Damage: ", base.throwMin, base.throwMax, true);
            if (!definition.equipment.twoHanded || definition.equipment.oneOrTwoHanded)
                damage("One-Hand Damage: ", base.minDamage, base.maxDamage);
            if (definition.equipment.twoHanded)
                damage("Two-Hand Damage: ", base.twoHandMin, base.twoHandMax);
        }
        if (definition.family == ItemFamily::Armor) {
            const int percent = sum("item_armor_percent");
            const int baseArmor = item.defense;
            line("Defense: " + std::to_string(baseArmor * std::max(0, 100 + percent) / 100 + sum("armorclass")));
        }
        if (definition.maxStack > 1 && !definition.equipment.isType("gold"))
            line("Quantity: " + std::to_string(item.quantity));
        if (definition.bookCapacity)
            line("Quantity: " + std::to_string(item.charges));
        if (definition.maxDurability && !definition.equipment.throwable)
            line("Durability: " + std::to_string(item.durability) + " of " +
                 std::to_string(context.maximumDurability));
        if (definition.beltRows)
            line("Belt capacity: " + std::to_string(4 * definition.beltRows));
        if (const auto *potion = content.potion(item.definition)) {
            if (potion->kind == PotionKind::Healing)
                line("Heals " + std::to_string(int(potion->amount)) + " Life");
            else if (potion->kind == PotionKind::Mana)
                line("Restores " + std::to_string(int(potion->amount)) + " Mana");
            else if (potion->kind == PotionKind::Rejuvenation)
                line("Restores " + std::to_string(int(potion->amount * 100)) + "% Life and Mana");
            else if (potion->kind == PotionKind::Stamina)
                line("Restores Stamina");
            if (potion->curesPoison) line("Cures Poison");
            if (potion->curesCold) line("Cures Cold");
            if (potion->seconds > 0) line("Duration: " + std::to_string(int(potion->seconds)) + " seconds");
        }
        auto requirement = [&](const char *label, int required, int actual) {
            if (required > 0) line(std::string(label) + std::to_string(required), actual < required ? ItemTextTone::Error : ItemTextTone::Normal);
        };
        auto required = [&](std::optional<int> value) {
            const int baseValue = value.value_or(0);
            return std::max(0, baseValue + baseValue * sum("item_req_percent") / 100 - ((item.nativeFlags & 0x400000u) ? 10 : 0));
        };
        requirement("Required Strength: ", required(base.requiredStrength), context.strength);
        requirement("Required Dexterity: ", required(base.requiredDexterity), context.dexterity);
        requirement("Required Level: ", std::max({base.requiredLevel.value_or(0), item.requiredLevel, item.socketRequiredLevel}), context.level);
        if (!item.identified) line("Unidentified", ItemTextTone::Error);
        else {
            for (auto &description : describeItemStats(content, item, context.level))
                line(std::move(description), ItemTextTone::Property);
            if (item.grantedSkill >= 0)
                if (const auto *skill = content.skills.find(item.grantedSkill))
                    line("+1 to " + skill->name, ItemTextTone::Property);
        }
        if (item.definition == "bkd") {
            line("Cairn Stones order:");
            const auto &objects = content.tables.at("objects");
            for (int objectClass : context.cainStones)
                for (size_t row = 0; row < objects.rows().size(); ++row)
                    if (objects.number(row, "Id").value_or(-1) == objectClass)
                        line(std::string(objects.value(row, "Name")));
        }
    }
    return display;
}
} // namespace d2x

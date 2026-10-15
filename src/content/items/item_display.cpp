#include "content/items/item_display.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_properties.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/consumables/potions.hpp"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <utility>

namespace d2x {
namespace {
std::string itemText(const ClassicData &content, std::string_view key) {
    const auto found = content.itemStrings.find(key);
    if (found == content.itemStrings.end()) return {};
    auto result = found->second;
    const auto first = result.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return result.substr(first, result.find_last_not_of(" \t\r\n") - first + 1);
}
std::string nameFormat(std::string pattern, std::initializer_list<std::string> words) {
    size_t index = 0;
    for (const auto &word : words) {
        const auto marker = "%" + std::to_string(index++);
        const auto at = pattern.find(marker);
        if (at != std::string::npos) pattern.replace(at, marker.size(), word);
    }
    while (pattern.find("  ") != std::string::npos) pattern.replace(pattern.find("  "), 2, " ");
    const auto first = pattern.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? std::string{} : pattern.substr(first, pattern.find_last_not_of(" \t\r\n") - first + 1);
}
const SpecialItemRecord *specialItem(const ClassicData &content, const ItemInstance &item) {
    if (item.specialRow < 0 || (item.quality != ItemQuality::Unique && item.quality != ItemQuality::Set))
        return nullptr;
    const auto &records = item.quality == ItemQuality::Unique ? content.uniqueItems
                                                                : content.setItems;
    auto found = std::find_if(records.begin(), records.end(),
                              [&](const auto &record) { return int32_t(record.row) == item.specialRow; });
    return found == records.end() ? nullptr : &*found;
}
std::string weaponClassText(const ClassicData &content, const ItemDefinition &definition) {
    // D2Client 1.13c RVA 8F9E0 / E0EF0: first matching ItemTypes ancestor.
    for (const auto &[type, key] : {std::pair{"staf", "WeaponDescStaff"}, {"axe", "WeaponDescAxe"},
             {"swor", "WeaponDescSword"}, {"knif", "WeaponDescDagger"}, {"tpot", "WeaponDescThrownPotion"},
             {"jave", "WeaponDescJavelin"}, {"spea", "WeaponDescSpear"}, {"bow", "WeaponDescBow"},
             {"pole", "WeaponDescPoleArm"}, {"xbow", "WeaponDescCrossBow"}, {"h2h", "WeaponDescH2H"},
             {"h2h2", "WeaponDescH2H"}, {"orb", "WeaponDescStaff"}, {"wand", "WeaponDescStaff"}, {"blun", "WeaponDescMace"}})
        if (definition.equipment.isType(type)) return itemText(content, key);
    return {};
}
std::string weaponSpeedText(const ClassicData &content, const ItemDefinition &definition,
                            std::string_view classCode, int increasedAttackSpeed) {
    const auto character = std::find_if(content.characters.begin(), content.characters.end(),
        [&](const auto &entry) { return entry.code == classCode; });
    if (character == content.characters.end()) return {};
    static constexpr std::string_view classCodes[]{"ama", "sor", "nec", "pal", "bar", "dru", "ass"};
    const auto classId = std::find(std::begin(classCodes), std::end(classCodes), classCode);
    if (classId == std::end(classCodes)) return {};
    const auto &equipment = definition.equipment;
    const auto &weapon = equipment.twoHanded && !canUseWeaponOneHanded(equipment, classCode)
        ? equipment.twoHandWeaponClass : definition.base.weaponClass;
    const auto animation = content.skills.attackTimings.find(character->appearance + "a1" + weapon);
    if (animation == content.skills.attackTimings.end()) return {};
    // ITEMS_GetWeaponAttackSpeed: raw A1 AnimData frame count/rate, base
    // Weapons.speed and this item's IAS, rather than the player's combat EIAS.
    const int rate = int(int64_t(animation->second.speed) * (100 - definition.base.speed.value_or(0) + increasedAttackSpeed) / 100);
    if (rate <= 0) return {};
    const int ticks = animation->second.frames * 256 / rate;
    // Original client classification tables at RVA E0F78 / E10E0. These are
    // client algorithms; animation lengths, weapon speed and labels remain MPQ data.
    static constexpr int offsets[]{0, 2, 1, 4, 1, 4, 0, 3, 0, 3, 1, 4, 0, 3};
    static constexpr int categories[]{
        1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 1,1,2,1,1,
        2,1,2,2,1, 2,1,2,2,2, 2,2,3,2,2, 3,2,3,3,2,
        3,2,3,3,3, 3,2,4,3,3, 4,3,4,4,3, 4,3,4,4,4,
        4,3,5,4,4, 5,4,5,5,4, 5,4,5,5,5, 5,4,5,5,5,
        5,5,5,5,5, 5,5,5,5,5};
    int category = 1;
    if (ticks >= 28) category = 5;
    else if (ticks >= 10) {
        const bool bow = equipment.isType("xbow") || equipment.isType("bow");
        category = categories[(ticks - 10) * 5 + offsets[size_t(classId - std::begin(classCodes)) * 2 + size_t(bow)]];
    }
    static constexpr const char *labels[]{"WeaponAttackFastest", "WeaponAttackVeryFast", "WeaponAttackFast",
        "WeaponAttackNormal", "WeaponAttackSlow", "WeaponAttackVerySlow"};
    return itemText(content, labels[category]);
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
        return nameFormat(itemText(content, "HiqualityFormat"), {itemText(content, "Hiquality"), name});
    if (item.quality == ItemQuality::Inferior) {
        auto found = std::find_if(content.inferiorGrades.begin(),
                                  content.inferiorGrades.end(),
                                  [&](const auto &record) { return int32_t(record.row) == item.gradeRow; });
        if (found != content.inferiorGrades.end())
            return nameFormat(itemText(content, "LowqualityFormat"), {localized(found->name), name});
    }
    if ((item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted || item.quality == ItemQuality::Tempered)) {
        auto rareName = [&](const auto &records, int32_t row) -> std::string {
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == row; });
            return found == records.end() ? std::string{} : localized(found->name);
        };
        auto prefix = rareName(content.rarePrefixes, item.rarePrefixRow);
        auto suffix = rareName(content.rareSuffixes, item.rareSuffixRow);
        if (!prefix.empty() && !suffix.empty())
            return nameFormat(itemText(content, "RareFormat"), {prefix, suffix});
    }
    if (item.quality == ItemQuality::Magic) {
        std::string prefix, suffix;
        for (const auto &affix : item.affixes) {
            const auto &records = affix.prefix ? content.magicPrefixes
                                                : content.magicSuffixes;
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == affix.row; });
            if (found != records.end())
                (affix.prefix ? prefix : suffix) = localized(found->name);
        }
        return nameFormat(itemText(content, "MagicFormat"), {prefix, name, suffix});
    }
    if (!item.socketedItems.empty())
        return nameFormat(itemText(content, "GemmedNormalName"), {itemText(content, "Gemmed"), name});
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
    display.tooltip.push_back({display.name, definition.equipment.isType("rune") ? ItemTextTone::RuneName : item.identified && item.runewordRow >= 0 ? ItemTextTone::RunewordName :
        plain && (item.sockets || (item.nativeFlags & 0x400000u)) ? ItemTextTone::SocketedName : ItemTextTone::Name});
    if (item.runewordRow >= 0 && item.identified) {
        display.tooltip.push_back({definition.name, ItemTextTone::RunewordName});
        std::string runes;
        for (const auto &child : item.socketedItems) {
            const auto name = itemText(content, child.definition + "L");
            if (name.empty()) { runes.clear(); break; }
            runes += name;
        }
        if (!runes.empty()) display.tooltip.push_back({"'" + runes + "'", ItemTextTone::RunewordName});
    }
    auto line = [&](std::string value, ItemTextTone tone = ItemTextTone::Normal) {
        if (!value.empty()) display.tooltip.push_back({std::move(value), tone});
    };
    auto baseLine = [&](std::string_view key, std::string value, bool improved = false) {
        const auto label = itemText(content, key);
        if (!label.empty()) display.tooltip.push_back({label + " " + value, ItemTextTone::Normal,
            improved ? label.size() + 1 : std::string::npos});
    };
    if (item.definition == "toa")
        if (const auto text = content.itemStrings.find("toa"); text != content.itemStrings.end())
            if (const auto separator = text->second.rfind('\n'); separator != std::string::npos)
                line(text->second.substr(0, separator));
    {
        if ((item.quality == ItemQuality::Unique || item.quality == ItemQuality::Set ||
             (item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted || item.quality == ItemQuality::Tempered)) && item.identified) line(definition.name, ItemTextTone::Name);
        auto stats = resolveItemStats(content, item, context.level);
        std::vector<ResolvedItemStat> activeSetStats;
        if (item.identified && item.quality == ItemQuality::Set && context.equipmentKnown &&
            std::find(context.equippedSetRows.begin(), context.equippedSetRows.end(), item.specialRow) != context.equippedSetRows.end())
            if (const auto *record = specialItem(content, item)) {
                std::vector<int32_t> members, worn;
                for (const auto &piece : content.setItems) if (piece.set == record->set) {
                    members.push_back(int32_t(piece.row));
                    if (std::find(context.equippedSetRows.begin(), context.equippedSetRows.end(), int32_t(piece.row)) != context.equippedSetRows.end())
                        worn.push_back(int32_t(piece.row));
                }
                const auto active = activeSetItemLayers(record->setAddFunction, item.specialRow, members, worn);
                ItemInstance layerItem;
                layerItem.nativeProperties = true;
                for (size_t layer = 0; layer < active.size(); ++layer)
                    if (active[layer]) layerItem.savedStats.insert(layerItem.savedStats.end(), item.savedSetStats[layer].begin(), item.savedSetStats[layer].end());
                activeSetStats = resolveOwnItemStats(content, layerItem, context.level);
            }
        auto sum = [&](const char *name) {
            int result = 0;
            for (const auto &stat : stats) if (stat.effect == name) result += stat.value;
            for (const auto &stat : activeSetStats) if (stat.effect == name) result += stat.value;
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
            // Ethereal base changes alone stay white; only item modifiers color the value.
            const bool improved = minimum > *low || maximum > std::max(*high, *low + 1);
            baseLine(label, std::to_string(minimum) + " " + itemText(content, "to") + " " + std::to_string(maximum), improved);
        };
        if (definition.family == ItemFamily::Weapon) {
            if (definition.equipment.throwable) damage("ItemStats1n", base.throwMin, base.throwMax, true);
            if (!definition.equipment.twoHanded || canUseWeaponOneHanded(definition.equipment, context.characterClass))
                damage("ItemStats1l", base.minDamage, base.maxDamage);
            if (definition.equipment.twoHanded)
                damage("ItemStats1m", base.twoHandMin, base.twoHandMax);
        }
        if (definition.family == ItemFamily::Armor) {
            const int percent = sum("item_armor_percent");
            const int baseArmor = item.defense;
            const int defense = std::max(0, baseArmor * std::max(0, 100 + percent) / 100 + sum("armorclass"));
            baseLine("ItemStats1h", std::to_string(defense), defense != baseArmor);
            if (definition.equipment.isType("shld")) {
                for (const auto &character : content.characters)
                    if (character.code == context.characterClass) {
                        const int block = std::clamp(base.block.value_or(0) + character.blockFactor + sum("toblock"), 0, 75);
                        baseLine("ItemStats1r", std::to_string(block) + "%", block > base.block.value_or(0));
                    }
                if (context.characterClass == "pal" && base.minDamage && base.maxDamage)
                    baseLine("ItemStats1o", std::to_string(*base.minDamage) + " " + itemText(content, "to") + " " + std::to_string(*base.maxDamage));
            }
            if (definition.equipment.isType("boot") && context.characterClass == "ass" && base.minDamage && base.maxDamage)
                baseLine("ModStre10k", std::to_string(*base.minDamage) + " " + itemText(content, "to") + " " + std::to_string(*base.maxDamage));
        }
        if (definition.maxStack > 1 && !definition.equipment.isType("gold"))
            baseLine("ItemStats1i", std::to_string(item.quantity));
        if (definition.bookCapacity)
            baseLine("ItemStats1i", std::to_string(item.charges));
        if (context.maximumDurability && !definition.equipment.throwable && !sum("item_indesctructible"))
            baseLine("ItemStats1d", std::to_string(item.durability) + " " + itemText(content, "ItemStats1j") + " " + std::to_string(context.maximumDurability));
        const auto &source = content.tables.at(base.sourceTable);
        const int spellDescription = source.number(base.sourceRow, "spelldesc").value_or(0);
        if (spellDescription == 1) line(itemText(content, source.value(base.sourceRow, "spelldescstr")));
        if (spellDescription == 2)
            if (const auto points = source.number(base.sourceRow, "spelldesccalc")) {
                int shown = *points;
                if (const auto *potion = content.potion(item.definition); potion &&
                    (potion->kind == PotionKind::Healing || potion->kind == PotionKind::Mana)) {
                    auto description = *potion;
                    description.amount = *points;
                    shown = potionRestorationFixed(description, context.characterClass) / 256;
                }
                baseLine(source.value(base.sourceRow, "spelldescstr"), std::to_string(shown));
            }
        if (const auto gem = content.socketGems.find(item.definition); gem != content.socketGems.end()) {
            line(itemText(content, "ExInsertSockets"));
            // Gems.txt has weapon, helm/body and shield programs. Read each
            // context separately; a loose gem must not look like an equipped bonus.
            for (const auto &[group, label] : {std::pair{0, "GemXp3"}, {1, "GemXp4"}, {1, "GemXp1"}, {2, "GemXp2"}}) {
                std::vector<ResolvedItemStat> bonuses;
                bool known = true;
                for (const auto &property : gem->second.properties[size_t(group)]) {
                    if (property.directRoll && property.minimum != property.maximum) { known = false; break; }
                    auto resolved = resolvePropertyStats(content, property, property.minimum.value_or(0), context.level, int(item.level));
                    bonuses.insert(bonuses.end(), resolved.begin(), resolved.end());
                }
                if (!known) continue;
                auto descriptions = describeItemStats(content, bonuses);
                if (descriptions.empty()) continue;
                // Native 56330/560B0 prefixes the first property and leaves
                // loose-gem descriptions white, including the context label.
                descriptions.front() = itemText(content, label) + " " + descriptions.front();
                for (auto &description : descriptions) line(std::move(description));
            }
        }
        auto requirement = [&](const char *label, int required, int actual) {
            if (actual < required) display.requirementsMet = false;
            if (required > 0) line(itemText(content, label) + " " + std::to_string(required), actual < required ? ItemTextTone::Error : ItemTextTone::Normal);
        };
        auto required = [&](std::optional<int> value) {
            const int baseValue = value.value_or(0);
            return std::max(0, baseValue + baseValue * sum("item_req_percent") / 100 - ((item.nativeFlags & 0x400000u) ? 10 : 0));
        };
        if (!definition.equipment.requiredClass.empty())
            display.requirementsMet &= definition.equipment.requiredClass == context.characterClass;
        if (!definition.equipment.requiredClass.empty())
            for (const auto &character : content.characters)
                if (character.code == definition.equipment.requiredClass)
                    line(itemText(content, content.tables.at("charstats").value(character.sourceRow, "StrClassOnly")),
                        character.code == context.characterClass ? ItemTextTone::Normal : ItemTextTone::Error);
        requirement("ItemStats1f", required(base.requiredDexterity), context.dexterity);
        requirement("ItemStats1e", required(base.requiredStrength), context.strength);
        requirement("ItemStats1p", std::max({base.requiredLevel.value_or(0), item.requiredLevel, item.socketRequiredLevel}), context.level);
        if (definition.family == ItemFamily::Weapon) {
            auto weaponClass = weaponClassText(content, definition);
            const auto speed = weaponSpeedText(content, definition, context.characterClass, sum("item_fasterattackrate"));
            if (!weaponClass.empty()) {
                if (speed.empty()) line(std::move(weaponClass));
                else {
                    weaponClass += " - ";
                    const auto offset = weaponClass.size();
                    display.tooltip.push_back({weaponClass + speed, ItemTextTone::Normal,
                        sum("item_fasterattackrate") ? offset : std::string::npos});
                }
            }
        }
        if (definition.equipment.isType("char")) line(itemText(content, "Charmdes"));
        if (definition.opensCube) line(itemText(content, "RightClicktoOpen"));
        else if (definition.usable || definition.bookCapacity) line(itemText(content, "RightClicktoUse"));
        if (!item.identified) line(itemText(content, "ItemStats1b"), ItemTextTone::Error);
        else {
            if (item.grantedSkill >= 0)
                stats.push_back({"item_singleskill", "item_singleskill", 1, item.grantedSkill, 1});
            if (definition.equipment.isType("blun"))
                // D2Client 1.13c 54744..54768 adds the intrinsic blunt bonus.
                stats.push_back({"item_undeaddamage_percent", "item_undeaddamage_percent", 50, 0, 50});
            for (auto &description : describeItemStats(content, stats, context.characterClass)) line(std::move(description), ItemTextTone::Property);
        }
        if ((item.nativeFlags & 0x400000u) || item.sockets) {
            auto footer = item.nativeFlags & 0x400000u ? itemText(content, "itemEthereal") : std::string{};
            if (item.sockets) {
                if (!footer.empty()) footer += ", ";
                footer += itemText(content, "Socketable") + " (" + std::to_string(item.sockets) + ")";
            }
            line(std::move(footer), ItemTextTone::Property);
        }
        if (item.identified && item.quality == ItemQuality::Set && context.equipmentKnown)
            if (const auto *record = specialItem(content, item)) {
                std::vector<int32_t> members, worn;
                for (const auto &piece : content.setItems) if (piece.set == record->set) {
                    members.push_back(int32_t(piece.row));
                    if (std::find(context.equippedSetRows.begin(), context.equippedSetRows.end(), int32_t(piece.row)) != context.equippedSetRows.end())
                        worn.push_back(int32_t(piece.row));
                }
                const bool equipped = std::find(worn.begin(), worn.end(), item.specialRow) != worn.end();
                if (equipped) {
                    for (auto &description : describeItemStats(content, activeSetStats, context.characterClass)) line(std::move(description), ItemTextTone::SetProperty);
                    std::vector<ResolvedItemStat> bonuses;
                    for (const auto &bonus : record->setBonuses) {
                        if (bonus.perItem || (bonus.pieces ? worn.size() < size_t(bonus.pieces) : worn.size() != members.size())) continue;
                        const auto &property = bonus.property;
                        if (property.directRoll && property.minimum != property.maximum) continue;
                        auto resolved = resolvePropertyStats(content, property, property.minimum.value_or(0), context.level, int(item.level));
                        bonuses.insert(bonuses.end(), resolved.begin(), resolved.end());
                    }
                    for (auto &description : describeItemStats(content, bonuses)) line(std::move(description), ItemTextTone::RunewordName);
                }
                display.tooltip.push_back({});
                line(itemText(content, record->set), ItemTextTone::RunewordName);
                for (const auto &piece : content.setItems) if (piece.set == record->set)
                    line(itemText(content, piece.name), std::find(worn.begin(), worn.end(), int32_t(piece.row)) != worn.end()
                        ? ItemTextTone::SetProperty : ItemTextTone::Error);
            }
    }
    return display;
}
} // namespace d2x

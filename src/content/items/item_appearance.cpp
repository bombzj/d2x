#include "item_appearance.hpp"
#include "content/classic_data.hpp"
#include "gameplay/items/state.hpp"
#include <string_view>

namespace d2x {
namespace {
struct Pd2Edition {
    int fileIndex;
    std::string_view text, icon;
    int paletteGroup = 8;
    int groundColor = -1;
};
constexpr Pd2Edition pd2Editions[]{
    {0xd7, "StrAnniversaryEdition", "invchn_se", 8, 0},
    {0xf8, "StrAnniversaryEdition", "invcap_se", 8, 20},
    {0xfd, "StrAnniversaryEdition", "invkitu_se", 8, 9},
    {0x142, "StrAnniversaryEdition", "inv8wsu_se", 8, 9},
    {0x1c2, "StrAnniversaryEdition", "invwolfboss_se", 8, 3},
    {0xc6, "StrSpecialEdition", "invburiza_se", 8, 5},
    {0x181, "StrSpecialEdition", "invearthshifter_se", 8, 19},
    {0x17b, "StrSpecialEdition", "invgiantskull_se", 8, 16},
    {0xb8, "StrSpecialEdition", "invribcracker_se", 8, 0},
    {0x186, "StrSpecialEdition", "invheadhunter_se", 8, 3},
    {0x11d, "StrSpecialEdition", "invheraldofzakarum_se", 8, 10},
    {0x130, "StrSpecialEdition", "invfleshripper_se", 8, 9},
    {0x68, "StrSpecialEdition", "invchanceguards_se", 0},
    {0xe8, "StrSpecialEdition", "invvenomgrip_se", 0},
    {0xf0, "StrSpecialEdition", "invwartravellers_se"},
    {0xfe, "StrSpecialEdition", "invhellslayer_se", 8, 8},
    {0x108, "StrSpecialEdition", "invstormspire_se", 8, 15},
    {0x15d, "StrSpecialEdition", "invmedusa_se", 8, 6},
    {0xf2, "StrSpecialEdition", "invstringofears_se", 0},
    {0xda, "StrSpecialEdition", "invguardianangel_se", 8, 4},
    {0x105, "StrSpecialEdition", "invgrandfather_se", 8, 9},
    {0xd1, "StrSpecialEdition", "invspiritshroud_se", 8, 0},
    {0x166, "StrSpecialEdition", {}, 7}
};
enum class ItemColorTarget { Inventory, Ground };
std::array<int, 2> itemColorTransform(const ClassicData &content, const ItemCatalog &catalog,
                                     const ItemInstance &item, ItemColorTarget target) {
    const auto *definition = catalog.find(item.definition);
    if (!definition) return {};
    const auto &base = content.tables.at(definition->base.sourceTable);
    const bool inventory = target == ItemColorTarget::Inventory;
    const int group = base.number(definition->base.sourceRow, inventory ? "InvTrans" : "Transform").value_or(0);
    auto transform = [&](int color) -> std::array<int, 2> {
        return group > 0 && group < 9 && color >= 0 && color < 21
            ? std::array{group, color} : std::array<int, 2>{};
    };
    auto colorCode = [&](std::string_view code) {
        const auto &colors = content.tables.at("colors");
        if (!code.empty())
            for (size_t row = 0; row < colors.rows().size(); ++row)
                if (colors.value(row, "Code") == code) return int(row);
        return -1;
    };
    if (content.profile == "remote-display-txt-v1" && item.nativeProperties) {
        int64_t dye = 0;
        for (const auto &stat : item.savedStats)
            if (stat.id == 477 && stat.parameter == 0) dye += stat.value;
        if (dye > 0) {
            if (group == 3 || group == 4) return {};
            return transform(dye <= 21 ? int(dye - 1) : 0);
        }
    }
    // D2Common 1.13c ordinal 10893 / RVA 251C0. Suffixes precede
    // prefixes; crafted/tempered items use the default (gem/auto) branch.
    if (item.quality == ItemQuality::Magic || item.quality == ItemQuality::Rare) {
        for (bool prefix : {false, true})
            for (const auto &affix : item.affixes) {
                if (affix.prefix != prefix || affix.row < 0) continue;
                const auto &table = content.tables.at(prefix ? "magicprefix" : "magicsuffix");
                if (size_t(affix.row) >= table.rows().size()) continue;
                const int color = colorCode(table.value(size_t(affix.row), "transformcolor"));
                if (color >= 0) return transform(color);
            }
    } else if (item.quality == ItemQuality::Unique || item.quality == ItemQuality::Set) {
        if (item.specialRow < 0) return {};
        const auto &table = content.tables.at(item.quality == ItemQuality::Unique ? "uniqueitems" : "setitems");
        if (size_t(item.specialRow) >= table.rows().size()) return {};
        return transform(colorCode(table.value(size_t(item.specialRow), inventory ? "invtransform" : "chrtransform")));
    } else if ((item.nativeFlags & 0x800u) && !item.socketedItems.empty()) {
        // Only the first inventory node is inspected, even if it is a rune/jewel.
        const auto &first = item.socketedItems.front();
        const auto *gem = catalog.find(first.definition);
        if (gem && gem->equipment.isType("gem")) {
            const auto &table = content.tables.at("gems");
            for (size_t row = 0; row < table.rows().size(); ++row)
                if (table.value(row, "code") == first.definition)
                    return transform(table.number(row, "transform").value_or(-1));
        }
    }
    if (item.nativeAutoAffix) {
        const auto &table = content.tables.at("automagic");
        if (item.nativeAutoAffix > table.rows().size()) return {};
        return transform(colorCode(table.value(item.nativeAutoAffix - 1, "transformcolor")));
    }
    return {};
}
} // namespace
std::string_view pd2ItemEditionText(int fileIndex) {
    for (const auto &edition : pd2Editions)
        if (edition.fileIndex == fileIndex) return edition.text;
    return {};
}
std::string pd2ItemInventoryArt(const ClassicData &content, const ItemInstance &item) {
    if (content.profile != "remote-display-txt-v1" || !item.nativeProperties ||
        !item.identified || item.quality != ItemQuality::Unique) return {};
    int64_t shiny = 0;
    for (const auto &stat : item.savedStats)
        if (stat.id == 270 && stat.parameter == 0) shiny += stat.value;
    if (shiny <= 0) return {};
    if (item.specialRow == 0x166) {
        int skill = -1;
        for (const auto &stat : item.savedStats)
            if (stat.id == 107 && stat.parameter >= 0 && (skill < 0 || stat.parameter < skill))
                skill = stat.parameter;
        const auto &skills = content.tables.at("skills");
        if (skill <= 0 || size_t(skill) >= skills.rows().size()) return {};
        const auto element = skills.value(size_t(skill), "EType");
        return std::string("data/global/items/invormus_") +
            (element == "ltng" ? "l" : element == "cold" ? "c" : "f") + "_se.dc6";
    }
    for (const auto &edition : pd2Editions)
        if (edition.fileIndex == item.specialRow && !edition.icon.empty())
            return "data/global/items/" + std::string(edition.icon) + ".dc6";
    return {};
}
std::array<int, 2> inventoryColorTransform(const ClassicData &content, const ItemCatalog &catalog,
                                          const ItemInstance &item) {
    if (!pd2ItemInventoryArt(content, item).empty()) return {};
    return itemColorTransform(content, catalog, item, ItemColorTarget::Inventory);
}
std::array<int, 2> groundColorTransform(const ClassicData &content, const ItemCatalog &catalog,
                                       const ItemInstance &item) {
    if (const auto art = pd2ItemInventoryArt(content, item); !art.empty()) {
        const auto *definition = catalog.find(item.definition);
        if (!definition) return {};
        for (const auto &edition : pd2Editions) {
            if (edition.fileIndex != item.specialRow) continue;
            const auto &base = content.tables.at(definition->base.sourceTable);
            const int group = edition.paletteGroup ? edition.paletteGroup :
                base.number(definition->base.sourceRow, "Transform").value_or(0);
            const int color = item.specialRow == 0x166 ?
                (art.ends_with("_l_se.dc6") ? 13 : art.ends_with("_c_se.dc6") ? 5 : 8) : edition.groundColor;
            return group > 0 && group < 9 && group != 3 && group != 4 && color >= 0 && color < 21
                ? std::array{group, color} : std::array<int, 2>{};
        }
    }
    // ITEMS_GetColor(NULL, item, ..., 0), as used by the world item renderer.
    return itemColorTransform(content, catalog, item, ItemColorTarget::Ground);
}
void loadItemAppearances(std::vector<ItemDefinition> &items,
                         const std::map<std::string, DataTable, std::less<>> &tables,
                         const std::vector<std::string> &armorTypes) {
    for (auto &item : items) {
        const auto &source = tables.at(item.base.sourceTable);
        auto &appearance = item.appearance;
        appearance.component = source.number(item.base.sourceRow, "component").value_or(-1);
        appearance.token = source.value(item.base.sourceRow, "alternategfx");
        if (appearance.token.empty())
            appearance.token = source.value(item.base.sourceRow, "alternateGfx");
        if (item.base.sourceTable != "armor" || appearance.component != 1)
            continue;
        constexpr std::array<const char *, 6> columns{"rArm", "lArm", "Torso", "Legs",
                                                       "rSPad", "lSPad"};
        for (size_t index = 0; index < columns.size(); ++index) {
            auto row = source.number(item.base.sourceRow, columns[index]);
            if (row && *row >= 0 && size_t(*row) < armorTypes.size())
                appearance.body[index] = armorTypes[*row];
        }
    }
}
} // namespace d2x

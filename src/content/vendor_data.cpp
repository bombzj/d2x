#include "vendor_data.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
std::map<std::string, VendorDefinition, std::less<>> loadVendorData(
    const std::map<std::string, DataTable, std::less<>> &tables, const ItemCatalog &catalog) {
    std::map<std::string, VendorDefinition, std::less<>> vendors;
    auto found = tables.find("npc");
    if (found == tables.end())
        return vendors;
    const auto &npc = found->second;
    if (!npc.has("npc") || !npc.has("sell mult") || !npc.has("buy mult") ||
        !npc.has("max buy") || !npc.has("max buy (N)") || !npc.has("max buy (H)"))
        throw std::runtime_error("Original NPC trade table lacks vendor pricing");
    const auto &pages = tables.at("storepage");
    const auto &types = tables.at("itemtypes");
    if (!pages.has("Code") || !pages.has("Store Page") || pages.rows().size() < 4 ||
        !types.has("Code") || !types.has("StorePage"))
        throw std::runtime_error("Original vendor page tables are incomplete");
    std::map<std::string, int, std::less<>> pageByCode;
    for (size_t row = 0; row < pages.rows().size(); ++row)
        if (!pages.value(row, "Code").empty())
            pageByCode.emplace(std::string(pages.value(row, "Code")), int(row));
    std::map<std::string, int, std::less<>> pageByType;
    for (size_t row = 0; row < types.rows().size(); ++row) {
        auto page = pageByCode.find(types.value(row, "StorePage"));
        if (!types.value(row, "Code").empty() && page != pageByCode.end())
            pageByType.emplace(std::string(types.value(row, "Code")), page->second);
    }
    for (size_t row = 0; row < npc.rows().size(); ++row) {
        std::string id(npc.value(row, "npc"));
        if (id.empty())
            continue;
        auto multiplier = npc.number(row, "sell mult");
        if (!multiplier || *multiplier <= 0)
            continue;
        std::string prefix = id;
        prefix.front() = char(std::toupper(static_cast<unsigned char>(prefix.front())));
        VendorDefinition vendor;
        vendor.id = id;
        vendor.sellMultiplier = *multiplier;
        vendor.buyMultiplier = npc.number(row, "buy mult").value_or(0);
        vendor.maxBuy = {npc.number(row, "max buy").value_or(0),
                         npc.number(row, "max buy (N)").value_or(0),
                         npc.number(row, "max buy (H)").value_or(0)};
        if (vendor.buyMultiplier <= 0 ||
            std::any_of(vendor.maxBuy.begin(), vendor.maxBuy.end(), [](int value) { return value <= 0; }))
            throw std::runtime_error("Original NPC buy price data is incomplete: " + id);
        vendor.repairMultiplier = npc.number(row, "rep mult").value_or(0);
        for (const auto suffix : {"A", "B", "C"})
            if (const int flag = npc.number(row, std::string("questflag ") + suffix).value_or(0))
                vendor.questPrices.push_back({flag,
                    npc.number(row, std::string("questsellmult ") + suffix).value_or(1024),
                    npc.number(row, std::string("questbuymult ") + suffix).value_or(1024),
                    npc.number(row, std::string("questrepmult ") + suffix).value_or(1024)});
        // Acts are engine service configuration (SUnitProxy), not localization.
        if (id == "fara" || id == "drognan" || id == "lysander" || id == "elzix") vendor.act = 1;
        if (id == "hratli" || id == "alkor" || id == "ormus" || id == "asheara") vendor.act = 2;
        if (id == "halbu" || id == "jamella") vendor.act = 3;
        if (id == "larzuk" || id == "malah" || id == "nihlathak" || id == "drehya") vendor.act = 4;
        for (auto family : {"weapons", "armor", "misc"}) {
            const auto &source = tables.at(family);
            for (size_t itemRow = 0; itemRow < source.rows().size(); ++itemRow) {
                auto code = std::string(source.value(itemRow, "code"));
                const auto *item = catalog.find(code);
                if (!item || item->base.sourceTable != family || item->base.sourceRow != itemRow)
                    continue;
                auto page = pageByType.find(item->base.type);
                if (page == pageByType.end())
                    continue;
                auto minColumn = prefix + "Min", maxColumn = prefix + "Max";
                if (!source.has(minColumn) || !source.has(maxColumn))
                    continue;
                auto minimum = source.number(itemRow, minColumn).value_or(0);
                auto maximum = source.number(itemRow, maxColumn).value_or(0);
                auto magicMinimum = source.number(itemRow, prefix + "MagicMin").value_or(0);
                auto magicMaximum = source.number(itemRow, prefix + "MagicMax").value_or(0);
                auto magicLevel = source.number(itemRow, prefix + "MagicLvl").value_or(255);
                bool permanent = source.number(itemRow, "PermStoreItem").value_or(0) != 0;
                // PermStoreItem only changes stocking behavior for NPCs whose own vendor column offers it.
                if (!item->base.spawnable.value_or(0) || (!maximum && !magicMaximum))
                    continue;
                if (minimum < 0 || maximum < minimum || magicMinimum < 0 ||
                    magicMaximum < magicMinimum || magicLevel < 0 || magicLevel > 255 ||
                    maximum > 255 || magicMaximum > 255)
                    throw std::runtime_error("Invalid original vendor quantity: " + code);
                vendor.items.push_back({code, item->base.level.value_or(0), minimum, maximum,
                                        magicMinimum, magicMaximum, magicLevel,
                                        page->second,
                                        permanent,
                                        (source.number(itemRow, "bitfield1").value_or(0) & 1) != 0});
            }
        }
        if (!vendor.items.empty())
            vendors.emplace(std::move(id), std::move(vendor));
    }
    return vendors;
}
} // namespace d2x

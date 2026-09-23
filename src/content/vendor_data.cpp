#include "vendor_data.hpp"
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
    if (!npc.has("npc") || !npc.has("sell mult"))
        throw std::runtime_error("Original NPC trade table lacks vendor pricing");
    for (size_t row = 0; row < npc.rows().size(); ++row) {
        std::string id(npc.value(row, "npc"));
        if (id.empty())
            continue;
        auto multiplier = npc.number(row, "sell mult");
        if (!multiplier || *multiplier <= 0)
            continue;
        std::string prefix = id;
        prefix.front() = char(std::toupper(static_cast<unsigned char>(prefix.front())));
        VendorDefinition vendor{id, *multiplier, {}};
        for (auto family : {"weapons", "armor", "misc"}) {
            const auto &source = tables.at(family);
            for (size_t itemRow = 0; itemRow < source.rows().size(); ++itemRow) {
                auto code = std::string(source.value(itemRow, "code"));
                const auto *item = catalog.find(code);
                if (!item || item->base.sourceTable != family || item->base.sourceRow != itemRow)
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
                if (!item->base.spawnable.value_or(0) || (!maximum && !magicMaximum && !permanent))
                    continue;
                if (minimum < 0 || maximum < minimum || magicMinimum < 0 ||
                    magicMaximum < magicMinimum || magicLevel < 0 || magicLevel > 255 ||
                    maximum > 255 || magicMaximum > 255)
                    throw std::runtime_error("Invalid original vendor quantity: " + code);
                vendor.items.push_back({code, item->base.level.value_or(0), minimum, maximum,
                                        magicMinimum, magicMaximum, magicLevel,
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

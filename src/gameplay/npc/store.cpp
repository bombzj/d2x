#include "store.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
unsigned below(uint64_t &random, unsigned bound) {
    random = uint64_t(uint32_t(random)) * 0x6ac690c5ULL + (random >> 32);
    return bound ? uint32_t(random) % bound : 0;
}
}
std::vector<VendorOffer> planVendorStock(const ClassicData &data, const VendorDefinition &vendor,
                                         unsigned playerLevel, uint64_t seed) {
    if (!playerLevel || playerLevel > 99 || vendor.sellMultiplier <= 0)
        throw std::runtime_error("Unsupported vendor stock parameters");
    // Act I normal difficulty cap and item level come from SUnitNpc.cpp.
    const unsigned level = std::min(playerLevel + 5, 12u);
    std::vector<VendorOffer> offers;
    unsigned randomCount = 0;
    // SUnitNpc.cpp creates normal/magic pools first, then a separate permanent
    // pool. A row may contribute to both pools.
    for (bool permanent : {false, true}) {
        for (const auto &rule : vendor.items) {
            if (permanent ? !rule.permanent : !rule.maximum)
                continue;
            const auto *item = data.items.find(rule.code);
            if (!item || !item->artAvailable || rule.level > int(level) ||
                item->equipment.isType("book") ||
                !item->base.cost || *item->base.cost < 0)
                continue;
            unsigned count = permanent ? 1u :
                unsigned(rule.minimum) + below(seed, unsigned(rule.maximum - rule.minimum + 1));
            for (unsigned index = 0; index < count; ++index) {
                if (!permanent && randomCount >= 32)
                    break;
                // The original vendor quality roll at level 5-9 makes values >85
                // superior. That price branch is deferred until its stat cost is
                // adapted; do not substitute a plain item for that roll.
                bool deferredQuality = !permanent &&
                    (level >= 10 ? below(seed, 100) >= 75 :
                     level >= 5 ? below(seed, 100) > 85 : below(seed, 100) > 90);
                if (!permanent) ++randomCount;
                if (deferredQuality)
                    continue;
                unsigned quantity = permanent && !item->equipment.quiver.empty()
                                        ? item->maxStack : 1;
                int defense = 0;
                uint64_t base = unsigned(*item->base.cost);
                if (item->family == ItemFamily::Armor) {
                    auto minimum = item->base.minDefense, maximum = item->base.maxDefense;
                    if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum || !*maximum)
                        continue;
                    defense = *minimum + int(below(seed, unsigned(*maximum - *minimum + 1)));
                    base = uint64_t(defense) * base / unsigned(*maximum);
                }
                // D2Common uses npc.txt SellMult for a player buying from a vendor.
                uint64_t price = base * unsigned(vendor.sellMultiplier) / 1024;
                if (!item->equipment.quiver.empty())
                    price = uint64_t(quantity) * unsigned(*item->base.cost) / 1024 *
                            unsigned(vendor.sellMultiplier) / 1024;
                else if (item->maxStack > 1)
                    price *= quantity;
                price = std::max<uint64_t>(1, price);
                if (price > std::numeric_limits<unsigned>::max())
                    continue;
                offers.push_back({uint32_t(offers.size() + 1), rule.code, quantity, level,
                                  unsigned(price), defense, permanent});
            }
            // MagicMin/MagicMax are retained in typed MPQ definitions. Magic
            // items require full bonus-stat pricing before they can be sold.
        }
    }
    return offers;
}
} // namespace d2x

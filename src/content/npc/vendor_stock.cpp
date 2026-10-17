#include "core/random.hpp"
#include "vendor_stock.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace d2x {
namespace {
unsigned below(uint64_t &random, unsigned bound) {
    return limitedRandom(random, bound);
}
}
bool npcCanRepair(std::string_view npc) {
    return npc == "charsi" || npc == "fara" || npc == "hratli" || npc == "halbu" || npc == "larzuk";
}
bool npcCanIdentify(std::string_view npc) {
    return npc == "cain2" || npc == "cain3" || npc == "cain4" || npc == "cain5" || npc == "cain6";
}
bool npcCanHeal(std::string_view npc) {
    return npc == "akara" || npc == "atma" || npc == "fara" || npc == "ormus" || npc == "jamella" || npc == "malah";
}
void generateVendorStock(const ClassicData &data,const VendorDefinition &vendor,
    unsigned playerLevel,int difficulty,uint64_t &seed,const VendorItemSource &create) {
    if (!playerLevel || playerLevel > 99 || difficulty < 0 || difficulty > 2 ||
        vendor.sellMultiplier <= 0 || vendor.act >= 5 || !create)
        throw std::runtime_error("Unsupported vendor stock parameters");
    constexpr std::array<unsigned, 5> caps{12, 20, 28, 36, 45};
    const unsigned level = difficulty == 0 ? std::min(playerLevel + 5, caps[vendor.act])
                                           : playerLevel + 5;
    unsigned failures = 0;
    auto generate = [&](const VendorItemRule &rule, ItemQuality quality) {
        const auto *item = data.items.find(rule.code);
        if (!item) { ++failures; return; }
        const auto &source = data.tables.at(item->base.sourceTable);
        const size_t sourceRow = item->base.sourceRow;
        auto upgrade = [&](const char *column) {
            if (const auto *next = data.items.find(source.value(sourceRow, column))) {
                item = next;
                return true;
            }
            return false;
        };
        // SUnitNpc: difficulty upgrades apply to the selected source record.
        if (difficulty && playerLevel > 25) {
            const auto roll = below(seed, 100000);
            if (difficulty == 1) {
                if (!(roll < 64 * level + 4000 && upgrade("ubercode"))) upgrade("NightmareUpgrade");
            } else {
                if (!(roll < 16 * level + 1000 && upgrade("ultracode")))
                    if (roll < 128 * level + 5000) upgrade("ubercode");
                upgrade("HellUpgrade");
            }
        }
        if(!create(item->code,quality,level,rule.permanent)) ++failures;
    };
    // MPQ NPC columns decide each vendor's distinct pool. Normal and magic
    // counts are independent; 32 is a failure limit, not a stock-size limit.
    for (const auto &rule : vendor.items) {
        if (rule.permanent || rule.level > int(level)) continue;
        unsigned count = level < 25 ? unsigned(rule.minimum) +
            below(seed, unsigned(rule.maximum - rule.minimum + 1)) : 0;
        for (unsigned index = 0; index < count; ++index) {
            const unsigned roll = below(seed, 100);
            auto quality = level >= 10 && roll >= 75 ? ItemQuality::Superior :
                level >= 5 && level < 10 && roll > 85 ? ItemQuality::Superior :
                level < 5 && roll > 90 ? ItemQuality::Inferior : ItemQuality::Normal;
            generate(rule, quality);
            if (failures > 32) return;
        }
        if (rule.magicEligible && rule.magicLevel <= int(level)) {
            const unsigned extra = level < 25 ? 1 : 2 + below(seed, 2);
            count = unsigned(rule.magicMinimum) + below(seed,
                extra + unsigned(rule.magicMaximum - rule.magicMinimum));
            for (unsigned index = 0; index < count; ++index) generate(rule, ItemQuality::Magic);
        }
    }
    for (const auto &rule : vendor.items)
        if (rule.permanent) {
            generate(rule, ItemQuality::Normal);
            if (failures > 32) break;
        }
}
} // namespace d2x

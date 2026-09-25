#include "item_consumables.hpp"
#include <stdexcept>
#include <string>

namespace d2x {
void loadItemConsumables(ClassicData &data) {
    const auto &table = data.tables.at("misc");
    if (!table.has("pSpell") || !table.has("stat1") || !table.has("calc1") || !table.has("len"))
        throw std::runtime_error("Expansion Misc table lacks executable potion columns");
    for (size_t row = 0; row < table.rows().size(); ++row) {
        auto code = table.value(row, "code");
        const auto *item = data.items.find(code);
        if (!item || !item->usable)
            continue;
        auto spell = table.number(row, "pSpell");
        if (spell == 2 && item->equipment.isType("scro")) {
            data.portalScrolls.emplace(std::string(code));
            continue;
        }
        if (spell == 1 && item->equipment.isType("scro")) {
            data.identifyScrolls.emplace(std::string(code));
            continue;
        }
        auto stat = table.value(row, "stat1");
        auto amount = table.number(row, "calc1");
        auto frames = table.number(row, "len");
        if (!spell || !amount || *amount <= 0)
            continue;
        PotionDefinition potion{};
        if (*spell == 3 && stat == "hpregen") {
            // Barbarian's original healing potion class multiplier is an engine rule.
            potion = {PotionKind::Healing, float(*amount * 2), float(frames.value_or(0)) / 25.f};
        } else if (*spell == 3 && stat == "manarecovery") {
            potion = {PotionKind::Mana, float(*amount), float(frames.value_or(0)) / 25.f};
        } else if (*spell == 5 && stat == "hitpoints" && table.value(row, "stat2") == "mana" &&
                   table.number(row, "calc2") == amount) {
            potion = {PotionKind::Rejuvenation, float(*amount) / 100.f, 0};
        } else if (*spell == 9 && stat == "staminarecoverybonus" &&
                   table.value(row, "state") == "staminapot") {
            potion = {PotionKind::Stamina, 1, float(frames.value_or(0)) / 25.f};
        } else
            continue;
        if (potion.kind != PotionKind::Rejuvenation && potion.seconds <= 0)
            throw std::runtime_error("Invalid original potion duration: " + std::string(code));
        data.potions.emplace(std::string(code), potion);
    }
}
} // namespace d2x

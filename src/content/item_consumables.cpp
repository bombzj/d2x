#include "item_consumables.hpp"
#include <stdexcept>
#include <string>

namespace d2x {
void loadItemConsumables(ClassicData &data) {
    const auto &table = data.tables.at("misc");
    if (!table.has("pSpell") || !table.has("stat1") || !table.has("calc1") || !table.has("len") ||
        !table.has("state") || !table.has("cstate1") || !table.has("cstate2") ||
        !table.has("stat2") || !table.has("calc2") ||
        !table.has("stat3") || !table.has("calc3"))
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
        if (!spell || ((*spell != 6 && *spell != 9) && (!amount || *amount <= 0)))
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
        } else if (*spell == 6 || *spell == 9) {
            potion = {PotionKind::Remedy, 0, float(frames.value_or(0)) / 25.f};
            // SkillItem pSpell06 delegates to pSpell09: both read all three
            // stat/calc pairs. Unknown formulas/stats must not execute partially.
            bool supported = true;
            for (int index = 1; index <= 3; ++index) {
                const auto suffix = std::to_string(index);
                const auto name = table.value(row, "stat" + suffix);
                if (name.empty()) continue;
                const auto value = table.number(row, "calc" + suffix);
                int *target = nullptr;
                if (name == "staminarecoverybonus") target = &potion.modifiers.staminaRecoveryBonus;
                else if (name == "poisonresist") target = &potion.modifiers.poisonResist;
                else if (name == "coldresist") target = &potion.modifiers.coldResist;
                else if (name == "maxpoisonresist") target = &potion.modifiers.combat.poisonMaxResist;
                else if (name == "maxcoldresist") target = &potion.modifiers.combat.coldMaxResist;
                if (!value || !target) { supported = false; break; }
                *target = *value;
            }
            if (!supported) continue;
            if (potion.modifiers.staminaRecoveryBonus > 0) potion.kind = PotionKind::Stamina;
        } else
            continue;
        if (potion.kind != PotionKind::Rejuvenation && potion.seconds <= 0)
            throw std::runtime_error("Invalid original potion duration: " + std::string(code));
        if (*spell == 6 || *spell == 9) {
            const auto state = data.states.find(table.value(row, "state"));
            if (state == data.states.end())
                throw std::runtime_error("Missing original potion state: " + std::string(code));
            potion.state = state->second.definition;
            potion.durationFrames = EffectFrame(*frames);
            for (int index = 0; *spell == 6 && index < 2; ++index) {
                const auto name = table.value(row, index == 0 ? "cstate1" : "cstate2");
                if (name.empty()) continue;
                const auto cure = data.states.find(name);
                if (cure == data.states.end())
                    throw std::runtime_error("Missing original potion cure state: " + std::string(code));
                potion.cureStates[size_t(index)] = cure->second.definition.id;
                potion.curesPoison |= name == "poison";
                potion.curesCold |= name == "cold" || name == "freeze";
            }
        }
        data.potions.emplace(std::string(code), potion);
    }
}
} // namespace d2x

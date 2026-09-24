#include "equipment_combat.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
void add(int &target, int value) {
    auto sum = int64_t(target) + value;
    if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
        throw std::runtime_error("Combat property sum exceeds supported range");
    target = int(sum);
}
int parameter(const PropertyRange &property) {
    if (property.parameter.empty()) return 0;
    int value = 0;
    auto [end, error] = std::from_chars(property.parameter.data(),
                                        property.parameter.data() + property.parameter.size(), value);
    return error == std::errc{} && end == property.parameter.data() + property.parameter.size() ? value : 0;
}
void addStat(std::string_view stat, int value, CombatModifiers &m) {
    int *target = nullptr;
    if (stat == "damagepercent") target = &m.damagePercent;
    else if (stat == "item_mindamage_percent") target = &m.minimumDamagePercent;
    else if (stat == "item_maxdamage_percent") target = &m.maximumDamagePercent;
    else if (stat == "item_normaldamage") target = &m.normalDamage;
    else if (stat == "item_tohit_percent") target = &m.attackRatingPercent;
    else if (stat == "firemindam") target = &m.fireMinimum;
    else if (stat == "firemaxdam") target = &m.fireMaximum;
    else if (stat == "lightmindam") target = &m.lightningMinimum;
    else if (stat == "lightmaxdam") target = &m.lightningMaximum;
    else if (stat == "coldmindam") target = &m.coldMinimum;
    else if (stat == "coldmaxdam") target = &m.coldMaximum;
    else if (stat == "coldlength") target = &m.coldFrames;
    else if (stat == "magicmindam") target = &m.magicMinimum;
    else if (stat == "magicmaxdam") target = &m.magicMaximum;
    else if (stat == "poisonmindam") target = &m.poisonMinimum;
    else if (stat == "poisonmaxdam") target = &m.poisonMaximum;
    else if (stat == "poisonlength") target = &m.poisonFrames;
    else if (stat == "maxfireresist") target = &m.fireMaxResist;
    else if (stat == "maxlightresist") target = &m.lightningMaxResist;
    else if (stat == "maxcoldresist") target = &m.coldMaxResist;
    else if (stat == "maxpoisonresist") target = &m.poisonMaxResist;
    else if (stat == "maxmagicresist") target = &m.magicMaxResist;
    else if (stat == "damageresist") target = &m.physicalResist;
    else if (stat == "magicresist") target = &m.magicResist;
    else if (stat == "normal_damage_reduction") target = &m.flatPhysicalReduction;
    else if (stat == "magic_damage_reduction") target = &m.flatMagicReduction;
    else if (stat == "item_absorbfire_percent") target = &m.fireAbsorbPercent;
    else if (stat == "item_absorblight_percent") target = &m.lightningAbsorbPercent;
    else if (stat == "item_absorbcold_percent") target = &m.coldAbsorbPercent;
    else if (stat == "item_absorbmagic_percent") target = &m.magicAbsorbPercent;
    else if (stat == "item_absorbfire") target = &m.fireAbsorb;
    else if (stat == "item_absorblight") target = &m.lightningAbsorb;
    else if (stat == "item_absorbcold") target = &m.coldAbsorb;
    else if (stat == "item_absorbmagic") target = &m.magicAbsorb;
    else if (stat == "item_maxhp_percent") target = &m.lifePercent;
    else if (stat == "item_maxmana_percent") target = &m.manaPercent;
    else if (stat == "toblock") target = &m.blockBonus;
    else if (stat == "item_fasterattackrate") target = &m.fasterAttack;
    else if (stat == "item_fastercastrate") target = &m.fasterCast;
    else if (stat == "item_fastergethitrate") target = &m.fasterHitRecovery;
    else if (stat == "item_fasterblockrate") target = &m.fasterBlock;
    else if (stat == "lifedrainmindam") target = &m.lifeLeech;
    else if (stat == "manadrainmindam") target = &m.manaLeech;
    else if (stat == "item_crushingblow") target = &m.crushingBlow;
    else if (stat == "item_openwounds") target = &m.openWounds;
    else if (stat == "item_deadlystrike") target = &m.deadlyStrike;
    else if (stat == "item_magicbonus") target = &m.magicFind;
    else if (stat == "item_goldbonus") target = &m.goldFind;
    else if (stat == "item_poisonlengthresist") target = &m.poisonLengthResist;
    if (target) add(*target, value);
    else if (stat == "item_cannotbefrozen" && value) m.cannotBeFrozen = true;
    else if (stat == "item_halffreezeduration" && value) m.halfFreezeDuration = true;
}
} // namespace
void applyEquipmentCombatProperty(const ClassicData &content, const PropertyRange &property,
                                  int roll, EntityId item, bool weapon, CombatModifiers &mods) {
    auto found = std::find_if(content.properties.begin(), content.properties.end(),
                              [&](const auto &definition) { return definition.code == property.code; });
    if (found == content.properties.end()) return;
    for (const auto &op : found->operations) {
        int value = 0;
        switch (op.function) {
        case 1: case 2: case 3: case 8: value = roll; break;
        case 5: case 6: case 7: value = roll; break;
        case 15: value = property.minimum.value_or(0); break;
        case 16: value = property.maximum.value_or(0); break;
        case 17: value = parameter(property); break;
        default: continue;
        }
        if (op.function == 5 || (op.function == 15 && op.stat == "mindamage")) {
            add(weapon ? mods.weapons[item].minimum : mods.minimumDamage, value);
            continue;
        }
        if (op.function == 6 || (op.function == 16 && op.stat == "maxdamage")) {
            add(weapon ? mods.weapons[item].maximum : mods.maximumDamage, value);
            continue;
        }
        if (op.function == 7) {
            if (weapon) add(mods.weapons[item].enhancedDamage, value);
            else { add(mods.minimumDamagePercent, value); add(mods.maximumDamagePercent, value); }
            continue;
        }
        if (op.stat == "item_armor_percent" && !weapon) {
            add(mods.armorPercent[item], value);
            continue;
        }
        if (op.stat.empty()) continue;
        const bool known = std::any_of(content.itemStats.begin(), content.itemStats.end(),
                                       [&](const auto &s) { return s.name == op.stat && s.id.has_value(); });
        if (known) {
            int *weaponValue = nullptr;
            if (weapon) {
                auto &own = mods.weapons[item];
                if (op.stat == "firemindam") weaponValue = &own.fireMinimum;
                else if (op.stat == "firemaxdam") weaponValue = &own.fireMaximum;
                else if (op.stat == "lightmindam") weaponValue = &own.lightningMinimum;
                else if (op.stat == "lightmaxdam") weaponValue = &own.lightningMaximum;
                else if (op.stat == "coldmindam") weaponValue = &own.coldMinimum;
                else if (op.stat == "coldmaxdam") weaponValue = &own.coldMaximum;
                else if (op.stat == "coldlength") weaponValue = &own.coldFrames;
                else if (op.stat == "magicmindam") weaponValue = &own.magicMinimum;
                else if (op.stat == "magicmaxdam") weaponValue = &own.magicMaximum;
                else if (op.stat == "poisonmindam") weaponValue = &own.poisonMinimum;
                else if (op.stat == "poisonmaxdam") weaponValue = &own.poisonMaximum;
                else if (op.stat == "poisonlength") weaponValue = &own.poisonFrames;
                else if (op.stat == "item_deadlystrike") weaponValue = &own.deadlyStrike;
            }
            if (weaponValue) add(*weaponValue, value);
            else addStat(op.stat, value, mods);
            if (op.stat == "poisonmaxdam" && value > 0)
                add(weapon ? mods.weapons[item].poisonSources : mods.poisonSources, 1);
        }
    }
}
} // namespace d2x

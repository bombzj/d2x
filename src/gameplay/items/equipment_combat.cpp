#include "equipment_combat.hpp"
#include <algorithm>
#include <cstdint>
#include <string_view>
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
void addStat(std::string_view stat, int value, CombatModifiers &m) {
    int *target = nullptr;
    if (stat == "damagepercent") target = &m.damagePercent;
    else if (stat == "attackrate") target = &m.attackRate;
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
    else if (stat == "passive_cold_mastery") target = &m.coldSkillDamagePercent;
    else if (stat == "passive_cold_pierce") target = &m.coldPierce;
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
    else if (stat == "curse_resistance") target = &m.curseResistance;
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
    else if (stat == "item_addexperience") target = &m.experiencePercent;
    else if (stat == "item_reducedprices") target = &m.reducedPrices;
    else if (stat == "item_poisonlengthresist") target = &m.poisonLengthResist;
    else if (stat == "hpregen") target = &m.replenishLife;
    else if (stat == "manarecoverybonus") target = &m.manaRecovery;
    else if (stat == "item_healafterkill") target = &m.lifeOnKill;
    else if (stat == "item_manaafterkill") target = &m.manaOnKill;
    else if (stat == "item_allskills") target = &m.allSkills;
    if (target) add(*target, value);
    else if (stat == "item_cannotbefrozen" && value) m.cannotBeFrozen = true;
    else if (stat == "item_halffreezeduration" && value) m.halfFreezeDuration = true;
}
} // namespace
void applyEquipmentStat(const ResolvedItemStat &resolved, EntityId item, bool weapon,
                         CombatModifiers &mods) {
    const auto &stat = resolved.effect;
    const int value = resolved.value;
    auto &target = weapon ? mods.weapons[item].target : mods.target;
    if (stat == "item_demondamage_percent") { add(target.demonDamage, value); return; }
    if (stat == "item_undeaddamage_percent") { add(target.undeadDamage, value); return; }
    if (stat == "item_demon_tohit") { add(target.demonAttackRating, value); return; }
    if (stat == "item_undead_tohit") { add(target.undeadAttackRating, value); return; }
    if (stat == "item_fractionaltargetac") { add(target.defenseReduction, value); return; }
    if (stat == "item_ignoretargetac") { target.ignoreDefense |= value != 0; return; }
    if (stat == "item_addclassskills") { add(mods.classSkills[resolved.layer], value); return; }
    if (stat == "item_singleskill") { add(mods.singleSkills[resolved.layer], value); return; }
    if (stat == "item_nonclassskill") { add(mods.nonClassSkills[resolved.layer], value); return; }
    if (stat == "item_addskill_tab") { add(mods.tabSkills[resolved.layer], value); return; }
    if (stat == "mindamage") { add(weapon ? mods.weapons[item].minimum : mods.minimumDamage, value); return; }
    if (stat == "maxdamage") { add(weapon ? mods.weapons[item].maximum : mods.maximumDamage, value); return; }
    if (weapon && stat == "item_normaldamage") { add(mods.weapons[item].normalDamage, value); return; }
    if (weapon && stat == "item_mindamage_percent") { add(mods.weapons[item].enhancedMinimum, value); return; }
    if (weapon && stat == "item_maxdamage_percent") { add(mods.weapons[item].enhancedMaximum, value); return; }
    if (stat == "item_armor_percent") {
        add(item ? mods.armorPercent[item] : mods.defensePercent, value);
        return;
    }
    int *ownValue = nullptr;
    if (weapon) {
        auto &own = mods.weapons[item];
        if (stat == "tohit") ownValue = &own.attackRating;
        else if (stat == "item_tohit_percent") ownValue = &own.attackRatingPercent;
        else if (stat == "item_fasterattackrate") ownValue = &own.fasterAttack;
        else if (stat == "firemindam") ownValue = &own.fireMinimum;
        else if (stat == "firemaxdam") ownValue = &own.fireMaximum;
        else if (stat == "lightmindam") ownValue = &own.lightningMinimum;
        else if (stat == "lightmaxdam") ownValue = &own.lightningMaximum;
        else if (stat == "coldmindam") ownValue = &own.coldMinimum;
        else if (stat == "coldmaxdam") ownValue = &own.coldMaximum;
        else if (stat == "coldlength") ownValue = &own.coldFrames;
        else if (stat == "magicmindam") ownValue = &own.magicMinimum;
        else if (stat == "magicmaxdam") ownValue = &own.magicMaximum;
        else if (stat == "poisonmindam") ownValue = &own.poisonMinimum;
        else if (stat == "poisonmaxdam") ownValue = &own.poisonMaximum;
        else if (stat == "poisonlength") ownValue = &own.poisonFrames;
        else if (stat == "item_deadlystrike") ownValue = &own.deadlyStrike;
        else if (stat == "lifedrainmindam") ownValue = &own.lifeLeech;
        else if (stat == "manadrainmindam") ownValue = &own.manaLeech;
        else if (stat == "item_crushingblow") ownValue = &own.crushingBlow;
        else if (stat == "item_openwounds") ownValue = &own.openWounds;
    }
    if (ownValue) add(*ownValue, value);
    else addStat(stat, value, mods);
    if (stat == "poisonmaxdam" && value > 0)
        add(weapon ? mods.weapons[item].poisonSources : mods.poisonSources, 1);
}
} // namespace d2x

#include "item_values.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/items/pd2_parameterized_stat.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <string>

namespace d2x {
std::array<int,3> pd2WeaponMastery(const StatSnapshot &stats,const StatRuleCatalog &rules,const WeaponDamage &weapon,
    const EquipmentStats *equipment,const SkillFormulaCatalog &formulas,bool dualWield,std::string_view group) {
    std::array<int,3> result{};
    constexpr std::array<std::string_view,3> suffixes{"th","dmg","crit"};
    for(size_t index=0;index<result.size();++index) {
        const auto name=std::string("passive_mastery_")+std::string(group)+"_"+std::string(suffixes[index]);
        // Core 3067B0 delegates every channel to 37F760: sum matching
        // parameter layers, including its two-hand/one-hand qualifications.
        result[index]=pd2ParameterizedItemStat(stats,rules.names.at(name),weapon,equipment,formulas.itemTypes,dualWield);
    }
    return result;
}
Pd2ItemModifiers pd2ItemModifiers(const StatSnapshot &values,const StatRuleCatalog &rules) {
    const auto stat=[&](std::string_view name){return logicalStatValue(values,rules,name);};
    return {stat("item_crit_chance"),stat("item_maxdeadlystrike"),stat("item_crit_multiplier"),
        stat("item_ds_multiplier"),stat("passive_phys_pierce"),stat("lifedrain_percentcap")};
}
void appendPd2ItemModifiers(StatList &list,const StatRuleCatalog &rules,const Pd2ItemModifiers &values) {
    addNativeStat(list,rules,"item_crit_chance",values.criticalChance);
    addNativeStat(list,rules,"item_maxdeadlystrike",values.maximumDeadlyStrike);
    addNativeStat(list,rules,"item_crit_multiplier",values.criticalMultiplier);
    addNativeStat(list,rules,"item_ds_multiplier",values.deadlyMultiplier);
    addNativeStat(list,rules,"passive_phys_pierce",values.physicalPierce);
    addNativeStat(list,rules,"lifedrain_percentcap",values.lifeDrainCap);
}
void applyPd2WeaponParameters(Pd2ItemModifiers &values,const StatSnapshot &stats,const StatRuleCatalog &rules,
    const WeaponDamage &weapon,const EquipmentStats *equipment,const SkillFormulaCatalog &formulas,bool dualWield) {
    values.criticalMultiplier=nativeAdd(values.criticalMultiplier,pd2ParameterizedItemStat(stats,
        rules.names.at("item_crit_multiplier"),weapon,equipment,formulas.itemTypes,dualWield));
    values.deadlyMultiplier=nativeAdd(values.deadlyMultiplier,pd2ParameterizedItemStat(stats,
        rules.names.at("item_ds_multiplier"),weapon,equipment,formulas.itemTypes,dualWield));
}
}

#include "passive_stats.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace d2x {
int nativeMonsterSkillRank(int base,int allSkills,int nonClassSkill,int singleSkill,int elementSkill,int levelBonus,int maximum) {
    if(maximum<1 || maximum>255) throw std::invalid_argument("Invalid native maximum skill level");
    const auto nonClass=base>0?std::min(3,nonClassSkill):nonClassSkill;
    return std::clamp(nativeAdd(nativeAdd(nativeAdd(base,levelBonus),nativeAdd(allSkills,nonClass)),nativeAdd(singleSkill,elementSkill)),0,maximum);
}
std::map<int,int> nativeMonsterSkillRanks(const std::map<int,int> &base,const StatSnapshot &stats,
    const StatRuleCatalog &rules,const SkillFormulaCatalog &formulas,int levelBonus) {
    auto assigned=base;
    // Item callbacks can assign an unowned zero-base skill slot. These slots
    // must win over an owned charged slot even when their effective rank is 0.
    // ItemMode's single-skill callback creates slots only for matching-class
    // players; a monster gets a new slot from nonclassskill only.
    for(const auto &[key,value]:stats.values) if(key.layer && value>0 && key.id==rules.names.at("item_nonclassskill"))
        assigned.try_emplace(key.layer,0);
    const auto layered=[&](std::string_view name,int layer) {return logicalStatValue(stats,rules,name,uint16_t(layer));};
    std::map<int,int> result;
    for(const auto &[id,rank]:assigned) {
        const auto record=formulas.skills.find(id);
        if(record==formulas.skills.end()) throw std::runtime_error("Missing native monster skill rank definition");
        const int element=record->second.elementType?layered("item_elemskill",record->second.elementType):0;
        result.emplace(id,nativeMonsterSkillRank(rank,logicalStatValue(stats,rules,"item_allskills"),
            layered("item_nonclassskill",id),layered("item_singleskill",id),element,levelBonus,formulas.maximumSkillRank));
    }
    return result;
}
bool refreshNativePassiveStats(const SkillFormulaCatalog &formulas,const StatRuleCatalog &rules,int id,int rank,
    const std::set<int> &states,SkillFormulaInput &context,StatList &list,const std::function<void()> &refresh) {
    const auto found=formulas.skills.find(id);
    if(found==formulas.skills.end()) throw std::runtime_error("Missing native passive definition");
    const auto &skill=found->second;
    if(rank<=0 || skill.passiveState<=0 || (skill.suppressionState>0 && states.contains(skill.suppressionState))) {
        if(!list.base.empty()) {list.base.clear();refresh();}
        return false;
    }
    const auto marker=rules.names.find("modifierlist_level");
    if(marker==rules.names.end()) throw std::runtime_error("Missing native passive level marker");
    const auto previous=list.base.find({marker->second,0});
    if(previous!=list.base.end() && previous->second==rank) return true;
    // The original keeps the old slots until each SetStat replaces its slot.
    // Subsequent formula slots observe those writes, including on refresh.
    for(int slot=1;slot<=5;++slot) {
        const auto index=std::to_string(slot);
        const auto stat=rules.names.find(skill.reference("passivestat"+index));
        if(stat==rules.names.end()) break;
        SkillFormulaValues values(formulas,context);
        const auto value=values.calculation(FormulaDomain::Skill,id,rank,"passivecalc"+index);
        if(!value) throw std::runtime_error("Unsupported native passive calculation: "+std::to_string(id));
        setNativeStat(list,rules,skill.reference("passivestat"+index),*value,skill.passiveLayer,true);
        refresh();
    }
    for(const auto &[name,value]:std::array<std::pair<std::string_view,int>,2>{{{"modifierlist_skill",id},{"modifierlist_level",rank}}}) {
        const auto stat=rules.names.find(name);
        if(stat==rules.names.end()) throw std::runtime_error("Missing native passive list marker");
        setNativeStat(list,rules,name,value,0,true);
        refresh();
    }
    return true;
}
}

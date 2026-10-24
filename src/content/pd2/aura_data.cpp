#include "aura_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/table_values.hpp"
#include <stdexcept>

namespace d2x {
AuraSkillSpec preparePd2Aura(const ClassicData &data,int skill) {
    const auto &table=data.tables.at("skills");
    const auto row=findSkillTableRow(table,"Id",std::to_string(skill));
    const auto number=[&](std::string_view field) {return skillTableNumber(table,row,field,SkillNumberPolicy::BlankIsZero);};
    AuraSkillSpec result;result.skill=skill;result.serverDoFunction=number("srvdofunc");
    result.mana=number("mana");result.manaPerLevel=number("lvlmana");result.manaShift=number("manashift");
    result.filter=uint32_t(number("aurafilter"));result.resultFlags=uint32_t(number("ResultFlags"))|0x20;
    result.hitClass=number("HitClass");result.periodFrames=number("perdelay");
    for(size_t index=0;index<result.parameters.size();++index) result.parameters[index]=number("Param"+std::to_string(index+1));
    if(const auto name=table.value(row,"aurastate");!name.empty()) result.ownerState=data.states.at(std::string(name)).definition;
    if(const auto name=table.value(row,"auratargetstate");!name.empty()) result.state=data.states.at(std::string(name)).definition;
    const auto element=table.value(row,"EType");
    result.element=element=="fire"?2:element=="ltng"?3:element=="cold"?4:element=="mag"?1:-1;
    // The host always calls evaluateNativeAura with the actual caster. Do not
    // translate PD2 formulas into retail's restricted AuraFormula vocabulary.
    return result;
}
void preparePd2Auras(ClassicData &data) {
    auto programs=std::make_shared<std::map<int,AuraSkillSpec>>();
    const auto &table=data.tables.at("skills");
    for(size_t row=0;row<table.rows().size();++row) {
        const auto id=table.number(row,"Id");if(!id) continue;
        const auto action=table.number(row,"srvdofunc").value_or(0);
        if(action!=65 && action!=66 && action!=81 && action!=82) continue;
        auto aura=preparePd2Aura(data,*id);
        if(aura.ownerState.id<0) throw std::runtime_error("Missing PD2 aura owner state");
        programs->emplace(*id,std::move(aura));
        if(const auto record=data.skills.skills.find(*id);record!=data.skills.skills.end()) {
            record->second.auraImplemented=true;
            record->second.auraImmediate=table.number(row,"immediate").value_or(0)!=0;
        }
    }
    data.nativeAuraPrograms=std::move(programs);
}
}

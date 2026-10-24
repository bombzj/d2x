#include "assassin_summon.hpp"
#include "formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
SummonCastSpec resolveAssassinSummon(const SummonSkillSpec &spec,int rank,int ownerLevel,int difficulty,
    SkillFormulaValues &values,int skill,bool pd2) {
    if(!spec.assassin || rank<1 || rank>255 || ownerLevel<1 || difficulty<0 || difficulty>2)
        throw std::runtime_error("Missing native assassin summon context");
    const auto calc=[&](std::string_view field) {
        const auto result=values.calculation(FormulaDomain::Skill,skill,rank,field);
        if(!result) throw std::runtime_error("Missing native assassin summon formula");
        return *result;
    };
    SummonCastSpec result;result.corpse=false;result.monster=spec.monster;result.kind=spec.kind;
    result.petType=spec.petType;result.limit=calc("petmax");result.stats=spec.base[size_t(difficulty)];
    const auto ai=spec.assassin->ai;
    const bool shadow=ai==AssassinPetDefinition::Ai::ShadowWarrior || ai==AssassinPetDefinition::Ai::ShadowMaster;
    auto pet=std::make_shared<AssassinPetSpec>();pet->definition=spec.assassin;pet->skill=skill;pet->rank=rank;
    pet->lifeFrames=shadow?calc("auralencalc"):0;
    pet->itemLevel=std::clamp(nativeAdd(values.skillNumber(skill,"param5"),nativeMultiply(rank-1,values.skillNumber(skill,"param6"))),1,99);
    auto &native=result.native.emplace();native.skill=skill;native.rank=rank;native.base=spec.base[size_t(difficulty)];
    native.setBaseStats=!shadow;
    const bool valkyrie=pd2 && shadow && !spec.assassin->inheritOwnerSkills && spec.assassin->summonFunction==16;
    if(shadow && !valkyrie) {
        result.stats.level=ownerLevel;native.base.level=ownerLevel;
        native.lifePercent=rank>1?nativeMultiply(values.skillNumber(skill,"param1"),rank-1):0;
        native.assassinShadow=true;
        native.individualShadowSlots=pd2;
        native.inheritedSkills=values.summonSkills(skill,rank);
    } else {
        result.stats.level=std::clamp(nativeAdd(rank,nativeMultiplyDivide(ownerLevel,3,4)),1,ownerLevel);
        const auto level=std::min(size_t(result.stats.level),spec.levelDefense.size()-1);
        native.levelDefense={spec.levelDefenseNonL.at(level)[size_t(difficulty)],spec.levelDefense.at(level)[size_t(difficulty)]};
        native.levelRating={spec.levelAttackNonL.at(level)[size_t(difficulty)],spec.levelAttack.at(level)[size_t(difficulty)]};
        native.passiveAdds=values.passiveBuffStats(skill,rank,false);
        native.auraWrites=values.auraStats(skill,rank,6,true,false);
        native.inheritedSkills=values.summonSkills(skill,rank);
        if(valkyrie) {
            native.setBaseStats=true;native.lifePercent=calc("calc1");
            pet->itemLevel=calc("calc2");
        }
        if(ai==AssassinPetDefinition::Ai::BladeCreeper) native.finalLevel=ownerLevel;
    }
    result.assassin=std::move(pet);return result;
}
}

#include "druid_summon.hpp"
#include "formula_parameters.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
// D2MOO SkillDruid Do114/115/119 and SkillNec SetSummonBase/PassiveStats.
SummonCastSpec resolveDruidSummon(const SummonSkillSpec &spec,int rank,int difficulty,SkillFormulaValues &values,int skill) {
    if(!spec.druid || rank<1 || rank>255 || difficulty<0 || difficulty>2 || spec.levelDefense.empty())
        throw std::runtime_error("Missing native druid summon context");
    const auto calc=[&](std::string_view field) {
        const auto result=values.calculation(FormulaDomain::Skill,skill,rank,field);
        if(!result) throw std::runtime_error("Missing native druid summon calculation");
        return *result;
    };
    SummonCastSpec result;result.corpse=false;result.monster=spec.monster;result.kind=spec.kind;
    result.petType=spec.petType;result.limit=calc("petmax");result.stats=spec.base[size_t(difficulty)];
    auto pet=std::make_shared<DruidPetSpec>();pet->definition=spec.druid;pet->skill=skill;pet->rank=rank;
    result.druid=std::move(pet);
    auto &native=result.native.emplace();native.skill=skill;native.rank=rank;native.base=result.stats;
    native.setBaseStats=!spec.druid->vine;
    result.stats.level=std::max(calc("calc2"),1);
    const auto level=std::min(size_t(result.stats.level),spec.levelDefense.size()-1);
    native.levelDefense={spec.levelDefenseNonL.at(level)[size_t(difficulty)],spec.levelDefense.at(level)[size_t(difficulty)]};
    native.levelRating={spec.levelAttackNonL.at(level)[size_t(difficulty)],spec.levelAttack.at(level)[size_t(difficulty)]};
    native.lifePercent=calc("calc1");native.passiveAdds=values.passiveBuffStats(skill,rank,false);
    native.auraWrites=values.auraStats(skill,rank,6,true,false);
    native.inheritedSkills=values.summonSkills(skill,rank);
    if(spec.druid->vine) native.prePassiveLevel=result.stats.level;
    return result;
}
}

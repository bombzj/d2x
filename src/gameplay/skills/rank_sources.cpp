#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/character/skill_rank.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/combat/stat_modifiers.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "formula_parameters.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
int nativeShrineSkillBonus(const SkillFormulaCatalog &formulas,const std::set<int> &states) {
    // D2Common::SKILLS_GetShrineSkillLevelBonus/GetBonusSkillLevel: the
    // bonus is an algorithmic +2 from the state, not Shrines.Arg0.
    return formulas.skillShrineState>=0 && states.contains(formulas.skillShrineState)?2:0;
}
std::optional<int> resolveNativePlayerSkillRank(SkillRankSourceRequest request,const StatSnapshot &stats,const StatRuleCatalog &rules,
    int elementType,int maximum,bool innate,int levelBonus,bool assigned) {
    if(request.skill<0 || request.skill>UINT16_MAX || request.classRow<0 || request.classRow>UINT16_MAX || maximum<1 || maximum>255)
        throw std::invalid_argument("Invalid native player skill rank context");
    const auto stat=[&](std::string_view name,int layer=0){return logicalStatValue(stats,rules,name,uint16_t(layer));};
    CharacterSkillRankInput input;input.learned=request.learned;input.native=request.native;
    input.granted=innate && request.learned<=0?1:0;
    input.singleSkill=stat("item_singleskill",request.skill);input.nonClassSkill=stat("item_nonclassskill",request.skill);
    input.assigned=assigned || request.learned>0 || innate || (request.native && input.singleSkill>0) || input.nonClassSkill>0;
    if(!input.assigned) return {};
    input.allSkills=stat("item_allskills");input.classSkills=stat("item_addclassskills",request.classRow);
    if(request.page>0) input.tabSkills=stat("item_addskill_tab",request.classRow*8+request.page-1);
    if(elementType>0) input.elementSkills=stat("item_elemskill",elementType);
    input.levelBonus=levelBonus;
    return std::min(resolveCharacterSkillRank(input),maximum);
}
int resolveSkillSourceRank(SkillRankSourceRequest request, std::span<const ItemSkillGrant> grants,
                          const CombatModifiers &bonuses) {
    auto bonus = [](const auto &values, int key) {
        const auto found = values.find(key);
        return found == values.end() ? 0 : found->second;
    };
    CharacterSkillRankInput input;
    input.learned = request.learned; input.native = request.native;
    for (const auto &grant : grants) if (grant.skill == request.skill) input.granted += grant.rank;
    input.singleSkill = bonus(bonuses.singleSkills, request.skill);
    input.nonClassSkill = bonus(bonuses.nonClassSkills, request.skill);
    input.allSkills = bonuses.allSkills;
    input.classSkills = bonus(bonuses.classSkills, request.classRow);
    if (request.page > 0) input.tabSkills = bonus(bonuses.tabSkills, request.classRow * 8 + request.page - 1);
    return resolveCharacterSkillRank(input);
}
} // namespace d2x

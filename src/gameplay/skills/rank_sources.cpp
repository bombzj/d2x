#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/character/skill_rank.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/combat/stat_modifiers.hpp"

namespace d2x {
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

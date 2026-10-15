#include "skill_rank.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
int resolveCharacterSkillRank(CharacterSkillRankInput i) {
    const bool assigned=i.assigned || i.learned>0 || i.granted>0 || (i.native && i.singleSkill>0) || i.nonClassSkill>0;
    if(!assigned) return 0;
    int rank = nativeAdd(i.learned,i.granted);
    // SKILLS_GetBonusSkillLevel runs for an existing unowned skill regardless
    // of intermediate sign, and GetSkillLevel clamps only the final sum.
    rank=nativeAdd(rank,nativeAdd(i.levelBonus,i.allSkills));
    if(i.native) rank=nativeAdd(rank,nativeAdd(i.classSkills,i.tabSkills));
    rank=nativeAdd(rank,i.native?std::min(3,i.nonClassSkill):i.nonClassSkill);
    rank=nativeAdd(rank,i.elementSkills);
    rank=nativeAdd(rank,i.singleSkill);
    return std::max(0, rank);
}
} // namespace d2x

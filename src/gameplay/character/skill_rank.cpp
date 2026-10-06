#include "skill_rank.hpp"
#include <algorithm>

namespace d2x {
int resolveCharacterSkillRank(CharacterSkillRankInput i) {
    int rank = i.learned + i.granted;
    if (i.native) rank += i.singleSkill;
    rank += i.native ? std::min(3, i.nonClassSkill) : i.nonClassSkill;
    if (rank > 0) {
        rank += i.allSkills;
        if (i.native) rank += i.classSkills + i.tabSkills;
    }
    return std::max(0, rank);
}
} // namespace d2x

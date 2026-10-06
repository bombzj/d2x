#pragma once

namespace d2x {
struct CharacterSkillRankInput {
    int learned = 0, granted = 0;
    bool native = false;
    int singleSkill = 0, nonClassSkill = 0, allSkills = 0, classSkills = 0, tabSkills = 0;
};
int resolveCharacterSkillRank(CharacterSkillRankInput input);
} // namespace d2x

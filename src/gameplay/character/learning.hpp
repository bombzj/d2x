#pragma once
#include "core/id.hpp"
#include "gameplay/character/skill_choices.hpp"
#include <array>
#include <map>
#include <span>
#include <string_view>

namespace d2x {
struct CharacterSkillContext {
    EntityId actor;
    std::string_view classCode;
    int level;
    bool alive;
    int &unspentSkills;
    std::map<int, int> &learned;
    std::array<SkillHotkey, 8> &hotkeys;
    std::array<int, 4> &selected;
    unsigned weaponSet;
};
struct SkillLearningRule {
    int id = -1;
    std::string_view classCode;
    int requiredLevel = 0, maximumRank = 0;
    std::span<const int> prerequisites;
};
struct SkillChoiceEligibility {
    int skill = -1;
    bool known = false, available = false, passive = false, leftAllowed = false;
};
int nextSkillRequiredLevel(const SkillLearningRule &rule, const std::map<int, int> &learned);
bool canLearnCharacterSkill(const CharacterSkillContext &context, const SkillLearningRule &rule);
bool learnCharacterSkill(CharacterSkillContext context, const SkillLearningRule &rule);
bool bindCharacterSkill(CharacterSkillContext context, unsigned index, bool right,
                        SkillChoiceEligibility choice);
bool selectCharacterSkill(CharacterSkillContext context, bool right, SkillChoiceEligibility choice);
bool resetCharacterSkills(CharacterSkillContext context, int rewardedPoints);
bool refundCharacterSkills(CharacterSkillContext context);
struct CharacterSkillRankInput {
    int learned = 0, granted = 0;
    bool native = false;
    int singleSkill = 0, nonClassSkill = 0, allSkills = 0, classSkills = 0, tabSkills = 0;
};
int resolveCharacterSkillRank(CharacterSkillRankInput input);
} // namespace d2x

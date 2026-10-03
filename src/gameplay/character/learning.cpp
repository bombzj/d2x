#include "learning.hpp"
#include <algorithm>

namespace d2x {
int nextSkillRequiredLevel(const SkillLearningRule &rule, const std::map<int, int> &learned) {
    const auto rank = learned.find(rule.id);
    return rule.requiredLevel + (rank == learned.end() ? 0 : rank->second);
}
bool canLearnCharacterSkill(const CharacterSkillContext &c, const SkillLearningRule &rule) {
    if (!c.actor || !c.alive || rule.id < 0 || rule.classCode != c.classCode ||
        c.unspentSkills <= 0 || c.level < nextSkillRequiredLevel(rule, c.learned)) return false;
    const auto current = c.learned.find(rule.id);
    if (current != c.learned.end() && current->second >= rule.maximumRank) return false;
    for (int prerequisite : rule.prerequisites) {
        const auto rank = c.learned.find(prerequisite);
        if (rank == c.learned.end() || rank->second <= 0) return false;
    }
    return true;
}
bool learnCharacterSkill(CharacterSkillContext c, const SkillLearningRule &rule) {
    if (!canLearnCharacterSkill(c, rule)) return false;
    ++c.learned[rule.id];
    --c.unspentSkills;
    return true;
}
namespace {
bool selectable(SkillChoiceEligibility choice, bool right) {
    return choice.skill < 0 || (choice.known && choice.available && !choice.passive && (right || choice.leftAllowed));
}
}
bool bindCharacterSkill(CharacterSkillContext c, unsigned index, bool right, SkillChoiceEligibility choice) {
    if (!c.actor || index >= c.hotkeys.size() || choice.skill < -2 || !selectable(choice, right)) return false;
    for (auto &key : c.hotkeys)
        if (key.skill == choice.skill && key.right == right) key.skill = -2;
    c.hotkeys[index] = {choice.skill, right};
    return true;
}
bool selectCharacterSkill(CharacterSkillContext c, bool right, SkillChoiceEligibility choice) {
    if (!c.actor || choice.skill < -1 || c.weaponSet > 1 || !selectable(choice, right)) return false;
    c.selected[c.weaponSet * 2 + unsigned(right)] = choice.skill;
    return true;
}
bool resetCharacterSkills(CharacterSkillContext c, int rewardedPoints) {
    if (!c.actor || !c.alive) return false;
    c.learned.clear();
    c.unspentSkills = c.level - 1 + rewardedPoints;
    return true;
}
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
bool refundCharacterSkills(CharacterSkillContext c) {
    if (!c.actor || !c.alive) return false;
    for (const auto &[skill, rank] : c.learned) c.unspentSkills += rank;
    c.learned.clear();
    c.hotkeys = {};
    return true;
}
} // namespace d2x

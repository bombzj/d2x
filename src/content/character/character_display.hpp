#pragma once
#include "gameplay/character/display.hpp"
#include <map>
#include <vector>

namespace d2x {
struct CharacterAttributes;
struct EquipmentStats;
struct SkillRecord;
struct SkillCastSpec;
struct AuraDefinition;
struct CharacterDisplayContext {
    const CharacterAttributes &attributes;
    const EquipmentStats &equipment;
    const std::map<int, int> &learned;
    int fireMastery = 0, lightningMastery = 0;
    bool weaponValuesKnown = false, spellDamageKnown = false;
};
CharacterActionDisplay describeCharacterAction(const CharacterDisplayContext &context,
    const SkillRecord *skill, int rank = 0, bool available = true);
std::vector<std::string> describeAuraSkill(const SkillRecord &skill, const AuraDefinition &aura, int baseRank);
std::vector<std::string> describeSkillPicker(const SkillRecord &skill, const SkillCastSpec *resolved,
    const AuraDefinition *aura, int baseRank, int aiCurseDivisor, bool valuesKnown = true, bool damageKnown = true);
} // namespace d2x

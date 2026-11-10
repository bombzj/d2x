#pragma once
#include "gameplay/character/display.hpp"
#include <map>

namespace d2x {
struct CharacterAttributes;
struct EquipmentStats;
struct SkillRecord;
struct SkillDescriptionCatalog;
struct SkillDescriptionInput;
struct KickPanelStats;
struct CharacterDisplayContext {
    const CharacterAttributes &attributes;
    const EquipmentStats &equipment;
    const std::map<int, int> &learned;
    int fireMastery = 0, lightningMastery = 0;
    bool weaponValuesKnown = false, spellDamageKnown = false;
    int concentrationDamagePercent = 0;
    bool dualWield = false;
    bool pd2 = false;
    const KickPanelStats *kick{};
};
CharacterActionDisplay describeCharacterAction(const CharacterDisplayContext &context,
    const SkillRecord *skill, int rank = 0, bool available = true);
CharacterActionDisplay describeCharacterAction(const CharacterDisplayContext &,const SkillDescriptionCatalog &,
    const SkillDescriptionInput &,const SkillRecord &,bool available);
} // namespace d2x

#include "gameplay/character/runtime_record.hpp"
#include "gameplay/model/state.hpp"
#include <utility>

namespace d2x {
CharacterRecord captureCharacterRecord(const PlayerState &player) {
    CharacterRecord record;
    record.id = player.id;
    record.name = player.name;
    record.characterClass = player.characterClass;
    record.nativeSaveSections = player.nativeSaveSections;
    record.hp = player.hp;
    record.mana = player.mana;
    record.stamina = player.stamina;
    record.weaponSet = player.weaponSet;
    record.gold = player.gold;
    record.bankGold = player.bankGold;
    record.npcIntroductions = player.npcIntroductions;
    record.experience = player.experience;
    record.level = player.level;
    record.allocated = player.allocated;
    record.unspentAttributes = player.unspentAttributes;
    record.skillRanks = player.skillRanks;
    record.unspentSkills = player.unspentSkills;
    record.skillHotkeys = player.skillHotkeys;
    record.selectedSkills = player.selectedSkills;
    record.actOneQuests = player.actOneQuests;
    const auto &merc = player.hireling;
    record.hireling = {merc.sourceRow, merc.classId, merc.nameKey, merc.level,
                      merc.hp, merc.experience, merc.seed};
    return record;
}

HirelingState restoreHirelingRecord(const HirelingRecord &record, Vec position) {
    HirelingState merc;
    merc.sourceRow = record.sourceRow;
    merc.classId = record.classId;
    merc.nameKey = record.nameKey;
    merc.level = record.level;
    merc.hp = record.hp;
    merc.experience = record.experience;
    merc.seed = record.seed;
    if (merc.sourceRow >= 0) merc.pos = position;
    return merc;
}

PlayerState restoreCharacterRecord(CharacterRecord record, Vec position) {
    PlayerState player;
    player.id = record.id;
    player.name = std::move(record.name);
    player.characterClass = std::move(record.characterClass);
    player.nativeSaveSections = std::move(record.nativeSaveSections);
    player.hp = record.hp;
    player.mana = record.mana;
    player.stamina = record.stamina;
    player.weaponSet = record.weaponSet;
    player.gold = record.gold;
    player.bankGold = record.bankGold;
    player.npcIntroductions = std::move(record.npcIntroductions);
    player.experience = record.experience;
    player.level = record.level;
    player.allocated = record.allocated;
    player.unspentAttributes = record.unspentAttributes;
    player.skillRanks = std::move(record.skillRanks);
    player.unspentSkills = record.unspentSkills;
    player.skillHotkeys = record.skillHotkeys;
    player.selectedSkills = record.selectedSkills;
    player.actOneQuests = record.actOneQuests;
    player.hireling = restoreHirelingRecord(record.hireling, position);
    player.pos = player.previous = position;
    return player;
}
} // namespace d2x

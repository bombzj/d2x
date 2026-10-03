#include "gameplay/character/runtime_record.hpp"
#include "gameplay/player/state.hpp"
#include <utility>

namespace d2x {
CharacterRecord captureCharacterRecord(const PlayerState &player) {
    CharacterRecord record;
    record.id = player.id;
    record.name = player.character.name;
    record.characterClass = player.character.characterClass;
    record.nativeSaveSections = player.character.nativeSaveSections;
    record.hp = player.resources.hp;
    record.mana = player.resources.mana;
    record.stamina = player.resources.stamina;
    record.weaponSet = player.character.weaponSet;
    record.gold = player.character.gold;
    record.bankGold = player.character.bankGold;
    record.npcIntroductions = player.character.npcIntroductions;
    record.experience = player.character.experience;
    record.level = player.character.level;
    record.allocated = player.character.allocated;
    record.unspentAttributes = player.character.unspentAttributes;
    record.skillRanks = player.character.skillRanks;
    record.unspentSkills = player.character.unspentSkills;
    record.skillHotkeys = player.character.skillHotkeys;
    record.selectedSkills = player.character.selectedSkills;
    record.actOneQuests = player.character.actOneQuests;
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
    player.character.name = std::move(record.name);
    player.character.characterClass = std::move(record.characterClass);
    player.character.nativeSaveSections = std::move(record.nativeSaveSections);
    player.resources.hp = record.hp;
    player.resources.mana = record.mana;
    player.resources.stamina = record.stamina;
    player.character.weaponSet = record.weaponSet;
    player.character.gold = record.gold;
    player.character.bankGold = record.bankGold;
    player.character.npcIntroductions = std::move(record.npcIntroductions);
    player.character.experience = record.experience;
    player.character.level = record.level;
    player.character.allocated = record.allocated;
    player.character.unspentAttributes = record.unspentAttributes;
    player.character.skillRanks = std::move(record.skillRanks);
    player.character.unspentSkills = record.unspentSkills;
    player.character.skillHotkeys = record.skillHotkeys;
    player.character.selectedSkills = record.selectedSkills;
    player.character.actOneQuests = record.actOneQuests;
    player.hireling = restoreHirelingRecord(record.hireling, position);
    player.movement.pos = player.movement.previous = position;
    return player;
}
} // namespace d2x

#include "respec.hpp"
#include <climits>
namespace d2x {
bool refundCharacterPoints(CharacterRecord &record) {
    int64_t skills=record.unspentSkills;
    const int64_t attributes=int64_t(record.unspentAttributes)+record.allocated.strength+record.allocated.dexterity+record.allocated.vitality+record.allocated.energy;
    for(const auto &[id,rank]:record.skillRanks) {(void)id;skills+=rank;}
    if(skills<0 || skills>INT32_MAX || attributes<0 || attributes>INT32_MAX) return false;
    record.unspentSkills=int(skills);record.unspentAttributes=int(attributes);
    record.skillRanks.clear();record.allocated={};record.selectedSkills.fill(-1);
    record.selectedSkillOwners.fill(UINT32_MAX);record.skillHotkeys={};
    return true;
}
}

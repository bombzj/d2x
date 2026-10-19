#include "respec.hpp"
#include "record.hpp"
#include <climits>
namespace d2x {
bool refundCharacterPoints(CharacterRecord &record,PointRefund refund) {
    const bool refundAttributes=refund==PointRefund::Attributes || refund==PointRefund::All;
    const bool refundSkills=refund==PointRefund::Skills || refund==PointRefund::All;
    if(!refundAttributes && !refundSkills) return false;
    int64_t skills=record.unspentSkills;
    const int64_t attributes=int64_t(record.unspentAttributes)+record.allocated.strength+record.allocated.dexterity+record.allocated.vitality+record.allocated.energy;
    if(refundSkills) for(const auto &[id,rank]:record.skillRanks) {(void)id;skills+=rank;}
    if((refundSkills && (skills<0 || skills>INT32_MAX)) || (refundAttributes && (attributes<0 || attributes>INT32_MAX))) return false;
    if(refundAttributes) {record.unspentAttributes=int(attributes);record.allocated={};}
    if(refundSkills) {
        record.unspentSkills=int(skills);record.skillRanks.clear();record.selectedSkills.fill(-1);
        record.selectedSkillOwners.fill(UINT32_MAX);record.skillHotkeys={};
    }
    return true;
}
}

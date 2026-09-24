#include "den_of_evil.hpp"

namespace d2x {
bool denAdvanceOnTalk(QuestRecord &record) {
    if (record.stage != uint32_t(DenStage::Unstarted)) return false;
    record.stage = uint32_t(DenStage::Assigned);
    return true;
}
bool denAdvanceOnEntry(QuestRecord &record) {
    if (record.stage >= uint32_t(DenStage::Entered)) return false;
    record.stage = uint32_t(DenStage::Entered);
    return true;
}
bool denAdvanceOnClear(QuestRecord &record, bool encounteredMonsters, bool monstersRemain) {
    if (!encounteredMonsters || monstersRemain || record.stage >= uint32_t(DenStage::Cleared))
        return false;
    record.stage = uint32_t(DenStage::Cleared);
    return true;
}
bool denClaimReward(QuestRecord &record) {
    if (record.stage != uint32_t(DenStage::Cleared)) return false;
    record.stage = uint32_t(DenStage::Rewarded);
    return true;
}
bool denClaimRespec(QuestRecord &record) {
    if (record.stage != uint32_t(DenStage::Rewarded) || (record.flags & denRespecUsed))
        return false;
    record.flags |= denRespecUsed;
    return true;
}
} // namespace d2x

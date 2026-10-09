#include "burial_grounds.hpp"

namespace d2x {
bool burialAdvanceOnTalk(QuestRecord &record, bool denCompleted) {
    if (record.stage != uint32_t(BurialStage::Unstarted) || !denCompleted) return false;
    record.stage = uint32_t(BurialStage::Assigned);
    return true;
}
bool burialAdvanceOnEntry(QuestRecord &record) {
    if (record.stage >= uint32_t(BurialStage::Entered)) return false;
    record.stage = uint32_t(BurialStage::Entered);
    return true;
}
bool burialAdvanceOnBloodRaven(QuestRecord &record) {
    if (record.stage >= uint32_t(BurialStage::BloodRavenSlain)) return false;
    record.stage = uint32_t(BurialStage::BloodRavenSlain);
    return true;
}
bool burialClaimReward(QuestRecord &record) {
    if (record.stage != uint32_t(BurialStage::BloodRavenSlain)) return false;
    record.stage = uint32_t(BurialStage::Rewarded);
    return true;
}
} // namespace d2x

#include "act_five_state.hpp"
#include "rules.hpp"
namespace d2x {
std::optional<NpcQuestPlan> actFiveConversation(QuestId id, const QuestRecord &record, const NpcQuestFacts &facts) {
    NpcQuestPlan plan{record};
    if (id == QuestId::EveOfDestruction && record.stage == 5) {
        plan.next.flags |= baalNpcAcknowledgement(facts.npcClass);
        if (facts.npcClass == "tyrael3") plan.reward = QuestReward::FinalPortal;
    }
    if (id == QuestId::RiteOfPassage) {
        if (!record.stage && facts.npcClass == "qual-kehk") plan.next.stage = 1;
        if (record.stage == 4) plan.next.flags |= ancientNpcAcknowledgement(facts.npcClass);
    }
    if (id == QuestId::BetrayalOfHarrogath && facts.npcClass == "drehya") {
        if (!record.stage) { plan.next.stage = 1; plan.reward = QuestReward::AnyaTemplePortal; }
        else if (record.stage == 3) plan.next.stage = 4;
    }
    if (id == QuestId::PrisonOfIce) {
        if (facts.npcClass == "malah") {
            if (!record.stage) plan.next.stage = 1;
            if (facts.questExplanation == 2 && record.stage >= 3 && record.stage < 5) { plan.next.stage = 4; plan.reward = QuestReward::DefrostPotion; }
            if (facts.questExplanation == 1 && record.stage >= 5) { plan.next.flags |= iceScrollGranted; plan.reward = QuestReward::ResistanceScroll; }
        }
        if (facts.npcClass == "drehya" && record.stage >= 5 && !(record.flags & iceRareGranted)) { plan.next.flags |= iceRareGranted; plan.reward = QuestReward::AnyaRare; }
        if (plan.next.stage >= 5 && (plan.next.flags & (iceScrollGranted | iceRareGranted)) == (iceScrollGranted | iceRareGranted)) plan.next.stage = 6;
    }
    if (id == QuestId::RescueOnMountArreat && facts.npcClass == "qual-kehk") {
        if (!record.stage) plan.next.stage = 1;
        else if (record.stage == 3) { plan.next.stage = 4; plan.reward = QuestReward::RescueRunes; }
    }
    if (id == QuestId::SiegeOnHarrogath && facts.npcClass == "larzuk") {
        if (!record.stage) plan.next.stage = 1;
        else if (record.stage == 3) plan.next.stage = 4;
    }
    return plan.next.stage != record.stage || plan.next.flags != record.flags || plan.reward != QuestReward::None ? std::optional{plan} : std::nullopt;
}
} // namespace d2x

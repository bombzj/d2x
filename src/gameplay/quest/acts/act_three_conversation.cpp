#include "act_three_state.hpp"
#include "rules.hpp"

namespace d2x {
std::optional<NpcQuestPlan> actThreeConversation(QuestId id, const QuestRecord &record, const NpcQuestFacts &facts) {
    NpcQuestPlan plan{record};
    if (id == QuestId::Guardian) {
        if (record.flags & guardianSpeechPending) plan.next.flags &= ~guardianSpeechPending;
        else if (!record.stage && facts.npcClass == "ormus") plan.next.stage = 1;
    }
    if (id == QuestId::BlackenedTemple) {
        if (facts.npcClass == "ormus" && !record.stage) plan.next.stage = 1;
        else if (facts.npcClass.starts_with("cain") && record.stage == 3) plan.next.stage = 4;
    }
    if (id == QuestId::LamEsensTome && facts.npcClass == "alkor") {
        if (!record.stage) plan.next.stage = 1;
        else if (record.stage == 3) { plan.next.stage = 4; plan.reward = QuestReward::LamTome; }
    }
    if (id == QuestId::GoldenBird) {
        if (facts.npcClass.starts_with("cain")) {
            if (record.stage == 1) plan.next.stage = 2;
            else if (record.stage == 3) plan.next.stage = 4;
        } else if (facts.npcClass == "meshif2" && (record.stage == 1 || record.stage == 2)) {
            plan.next.stage = 3; plan.reward = QuestReward::ExchangeGoldenBird;
        } else if (facts.npcClass == "alkor") {
            if (record.stage == 3 || record.stage == 4) {
                plan.next.stage = 5; plan.reward = QuestReward::DeliverGoldenBird;
            } else if (record.stage == 5) {
                plan.next.stage = 6; plan.next.flags = goldenBirdPotionPending; plan.reward = QuestReward::LifePotion;
            }
        }
    }
    if (id == QuestId::BladeOfTheOldReligion) {
        if (facts.npcClass == "hratli" && !record.stage) plan.next.stage = 1;
        if (facts.npcClass == "ormus" && record.stage == 3) {
            plan.next.stage = 4; plan.reward = QuestReward::ReturnGidbinn;
        } else if (record.stage == 4) {
            if (facts.npcClass == "ormus" && !(record.flags & gidbinnRingGranted)) {
                plan.next.flags |= gidbinnRingGranted; plan.reward = QuestReward::GidbinnRing;
            } else if (facts.npcClass == "asheara" && !(record.flags & gidbinnHirelingGranted)) {
                plan.next.flags |= gidbinnHirelingGranted; plan.reward = QuestReward::IronWolf;
            }
            if (plan.next.flags == (gidbinnRingGranted | gidbinnHirelingGranted)) plan.next.stage = 5;
        }
    }
    if (id == QuestId::KhalimsWill && facts.npcClass.starts_with("cain") && record.stage < 4 &&
        facts.questExplanation && !(facts.questExplanation & ~63u)) {
        plan.next.flags |= facts.questExplanation;
        if (!plan.next.stage) plan.next.stage = 1;
    }
    return plan.next.stage != record.stage || plan.next.flags != record.flags ? std::optional{plan} : std::nullopt;
}
} // namespace d2x

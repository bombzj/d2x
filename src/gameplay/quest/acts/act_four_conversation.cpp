#include "act_four_state.hpp"
#include "rules.hpp"
namespace d2x {
std::optional<NpcQuestPlan> actFourConversation(QuestId id, const QuestRecord &record, const NpcQuestFacts &facts) {
    NpcQuestPlan plan{record};
    if (id == QuestId::TerrorsEnd) {
        if (facts.npcClass == "tyrael2") {
            if (!record.stage) plan.next.stage = 1;
            if (record.stage == 4) { plan.next.flags = (record.flags & ~terrorTyraelPending) | terrorPortalOpened; plan.reward = QuestReward::ActFivePortal; }
        } else if (record.stage == 4 && facts.npcClass.starts_with("cain")) plan.next.flags &= ~terrorCainPending;
    }
    if (id == QuestId::HellsForge && facts.npcClass.starts_with("cain")) {
        if (record.stage == 4) plan.next.stage = 5;
        else if (record.stage < 3) {
            if (!record.stage) plan.next.stage = 1;
            if (facts.questExplanation) plan.reward = QuestReward::Soulstone;
        }
    }
    if (id == QuestId::FallenAngel) {
        if (facts.npcClass == "tyrael2" && !record.stage) plan.next.stage = 1;
        else if (facts.npcClass == "tyrael2" && record.stage == 3) { plan.next = {4, 0}; plan.reward = QuestReward::IzualSkills; }
        else if (facts.npcClass == "izualghost" && record.stage == 3) plan.next.flags |= izualGhostSpoken;
    }
    return plan.next.stage != record.stage || plan.next.flags != record.flags || plan.reward != QuestReward::None ? std::optional{plan} : std::nullopt;
}
} // namespace d2x

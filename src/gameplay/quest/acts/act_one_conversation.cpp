#include "rules.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"

namespace d2x {
std::optional<NpcQuestPlan> actOneConversation(QuestId id, const QuestRecord &record,
                                             const NpcQuestFacts &facts) {
    NpcQuestPlan plan{record};
    auto &next = plan.next;
    bool changed = false;
    const auto npc = facts.npcClass;
    switch (id) {
    case QuestId::DenOfEvil:
        if (npc != "akara") return std::nullopt;
        if (denClaimReward(next)) { plan.reward = QuestReward::SkillPoint; changed = true; }
        else changed = denAdvanceOnTalk(next);
        break;
    case QuestId::SistersBurialGrounds:
        if (npc != "kashya") return std::nullopt;
        if (record.stage == uint32_t(BurialStage::BloodRavenSlain)) {
            plan.reward = QuestReward::Rogue; changed = burialClaimReward(next);
        } else changed = burialAdvanceOnTalk(next, facts.denRewarded);
        break;
    case QuestId::SearchForCain:
        if (npc != "akara") return std::nullopt;
        if (record.stage == uint32_t(CainStage::BarkAcquired)) {
            plan.reward = QuestReward::TranslateScroll; changed = cainAdvance(next, CainStage::ScrollTranslated);
        } else if (record.stage == uint32_t(CainStage::Rescued)) {
            plan.reward = QuestReward::CainRing; changed = cainAdvance(next, CainStage::Rewarded);
        } else if (record.stage == uint32_t(CainStage::Unstarted)) changed = cainAdvance(next, CainStage::Assigned);
        break;
    case QuestId::ToolsOfTheTrade:
        if (npc != "charsi") return std::nullopt;
        if (record.stage == uint32_t(ToolsStage::MalusAcquired)) {
            plan.reward = QuestReward::ReturnMalus; changed = toolsAdvance(next, ToolsStage::RewardReady);
        } else if (record.stage == uint32_t(ToolsStage::Unstarted)) changed = toolsAdvance(next, ToolsStage::Assigned);
        break;
    case QuestId::SistersToTheSlaughter:
        if (record.stage == uint32_t(SlaughterStage::AndarielSlain) ? npc != "warriv1" : !npc.starts_with("cain"))
            return std::nullopt;
        if (record.stage == uint32_t(SlaughterStage::AndarielSlain)) changed = slaughterAdvance(next, SlaughterStage::PassageReady);
        else if (record.stage == uint32_t(SlaughterStage::Unstarted)) changed = slaughterAdvance(next, SlaughterStage::Assigned);
        break;
    default: break;
    }
    return changed ? std::optional{plan} : std::nullopt;
}
} // namespace d2x

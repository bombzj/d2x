#include "npc_conversation.hpp"
#include "den_of_evil.hpp"
#include "burial_grounds.hpp"
#include "search_for_cain.hpp"
#include "tools_of_trade.hpp"
#include "sisters_to_slaughter.hpp"
#include <algorithm>

namespace d2x {
std::optional<NpcQuestPlan> planNpcQuest(QuestId id, const QuestRecord &record,
                                       const NpcQuestFacts &facts) {
    NpcQuestPlan plan{record};
    auto &next = plan.next;
    bool changed = false;
    switch (id) {
    case QuestId::SevenTombs:
        if (facts.npcClass == "tyrael1" && record.stage == 2) {
            next.stage = 3; plan.reward = QuestReward::TyraelPortal;
        } else if (facts.npcClass == "jerhyn" && record.stage == 3) next.stage = 4;
        else if (facts.npcClass == "meshif1" && record.stage == 4) next.stage = 5;
        else if (facts.npcClass == "jerhyn" && !record.stage) next.stage = 1;
        else return std::nullopt;
        changed = true;
        break;
    case QuestId::Summoner:
        if (record.stage == 2) { next.stage = 3; changed = true; }
        break;
    case QuestId::TaintedSun:
        next.stage = record.stage == 3 ? 4 : 2; changed = true;
        break;
    case QuestId::ArcaneSanctuary:
        next.stage = std::max(record.stage, facts.npcClass == "drognan" ? 1u : 2u);
        changed = true;
        break;
    case QuestId::HoradricStaff:
        next.flags |= staffScrollExplained; changed = true;
        break;
    case QuestId::RadamentsLair:
        changed = radamentAdvance(next, record.stage == uint32_t(RadamentStage::Slain)
            ? RadamentStage::Rewarded : RadamentStage::Assigned);
        break;
    case QuestId::DenOfEvil:
        if (denClaimReward(next)) { plan.reward = QuestReward::SkillPoint; changed = true; }
        else changed = denAdvanceOnTalk(next);
        break;
    case QuestId::SistersBurialGrounds:
        if (record.stage == uint32_t(BurialStage::BloodRavenSlain)) {
            plan.reward = QuestReward::Rogue; changed = burialClaimReward(next);
        } else changed = burialAdvanceOnTalk(next, facts.denRewarded);
        break;
    case QuestId::SearchForCain:
        if (record.stage == uint32_t(CainStage::BarkAcquired)) {
            plan.reward = QuestReward::TranslateScroll; changed = cainAdvance(next, CainStage::ScrollTranslated);
        } else if (record.stage == uint32_t(CainStage::Rescued)) {
            plan.reward = QuestReward::CainRing; changed = cainAdvance(next, CainStage::Rewarded);
        } else if (record.stage == uint32_t(CainStage::Unstarted)) changed = cainAdvance(next, CainStage::Assigned);
        break;
    case QuestId::ToolsOfTheTrade:
        if (record.stage == uint32_t(ToolsStage::MalusAcquired)) {
            plan.reward = QuestReward::ReturnMalus; changed = toolsAdvance(next, ToolsStage::RewardReady);
        } else if (record.stage == uint32_t(ToolsStage::Unstarted)) changed = toolsAdvance(next, ToolsStage::Assigned);
        break;
    case QuestId::SistersToTheSlaughter:
        if (record.stage == uint32_t(SlaughterStage::AndarielSlain)) changed = slaughterAdvance(next, SlaughterStage::PassageReady);
        else if (record.stage == uint32_t(SlaughterStage::Unstarted)) changed = slaughterAdvance(next, SlaughterStage::Assigned);
        break;
    default: break;
    }
    return changed ? std::optional{plan} : std::nullopt;
}
} // namespace d2x

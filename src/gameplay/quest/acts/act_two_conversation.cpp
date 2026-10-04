#include "gameplay/quest/acts/act_two_state.hpp"
#include "rules.hpp"

namespace d2x {
std::optional<NpcQuestPlan> actTwoConversation(QuestId id, const QuestRecord &record, const NpcQuestFacts &facts) {
    NpcQuestPlan plan{record};
    auto &next = plan.next;
    switch (id) {
    case QuestId::SevenTombs:
        if (facts.npcClass == "tyrael1" && record.stage == uint32_t(TombsStage::DurielSlain)) {
            next.stage = uint32_t(TombsStage::TyraelRescued); plan.reward = QuestReward::TyraelPortal;
        } else if (facts.npcClass == "jerhyn" && record.stage == uint32_t(TombsStage::TyraelRescued))
            next.stage = uint32_t(TombsStage::JerhynConfirmed);
        else if (facts.npcClass == "meshif1" && record.stage == uint32_t(TombsStage::JerhynConfirmed))
            next.stage = uint32_t(TombsStage::PassageGranted);
        else if (facts.npcClass == "jerhyn" && !record.stage) next.stage = uint32_t(TombsStage::Assigned);
        break;
    case QuestId::Summoner:
        if (record.stage == uint32_t(SummonerStage::Slain)) next.stage = uint32_t(SummonerStage::Confirmed);
        break;
    case QuestId::TaintedSun:
        if (record.stage == uint32_t(SunStage::AltarDestroyed)) next.stage = uint32_t(SunStage::Confirmed);
        else if (facts.npcClass == "drognan" && record.stage == uint32_t(SunStage::Darkness)) next.stage = uint32_t(SunStage::Explained);
        break;
    case QuestId::ArcaneSanctuary:
        if (facts.npcClass == "drognan" && record.stage == uint32_t(ArcaneStage::Unstarted)) next.stage = uint32_t(ArcaneStage::PalaceOpened);
        else if (facts.npcClass == "jerhyn" && record.stage == uint32_t(ArcaneStage::PalaceOpened)) next.stage = uint32_t(ArcaneStage::JerhynBriefed);
        break;
    case QuestId::HoradricStaff:
        if (facts.npcClass.starts_with("cain") && record.stage < uint32_t(StaffStage::Submitted) &&
            facts.questExplanation && !(facts.questExplanation & ~staffExplanationMask)) {
            // A2Q2's item speeches also acknowledge LEAVETOWN; assembled-staff
            // speech acknowledges the cube, cap and shaft at the same time.
            next.flags |= facts.questExplanation | staffScrollExplained;
            if (facts.questExplanation & staffAssemblyExplained) next.flags |= staffExplanationMask;
        }
        break;
    case QuestId::RadamentsLair:
        if (facts.npcClass == "atma") {
            if (record.stage == uint32_t(RadamentStage::Unstarted)) radamentAdvance(next, RadamentStage::Assigned);
            else if (record.stage == uint32_t(RadamentStage::Slain)) radamentAdvance(next, RadamentStage::Rewarded);
        }
        break;
    default: break;
    }
    return next.stage != record.stage || next.flags != record.flags ? std::optional{plan} : std::nullopt;
}
} // namespace d2x

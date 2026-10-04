#include "rules.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"

namespace d2x {
namespace {
std::string_view descriptionKey(QuestId quest, uint32_t stage) {
    switch (quest) {
    case QuestId::DenOfEvil:
        if (stage == uint32_t(DenStage::Assigned)) return "qstsa1q11";
        if (stage == uint32_t(DenStage::Entered)) return "qstsa1q12";
        if (stage == uint32_t(DenStage::Cleared)) return "qstsa1q15";
        break;
    case QuestId::SistersBurialGrounds:
        if (stage == uint32_t(BurialStage::Assigned)) return "qstsa1q21";
        if (stage == uint32_t(BurialStage::Entered)) return "qstsa1q22";
        if (stage == uint32_t(BurialStage::BloodRavenSlain)) return "qstsa1q23";
        break;
    case QuestId::SearchForCain:
        if (stage == uint32_t(CainStage::Assigned)) return "qstsa1q41";
        if (stage == uint32_t(CainStage::TreeOpened)) return "qstsa1q41";
        if (stage == uint32_t(CainStage::BarkAcquired)) return "qstsa1q42";
        if (stage == uint32_t(CainStage::ScrollTranslated)) return "qstsa1q43";
        if (stage == uint32_t(CainStage::PortalOpened)) return "qstsa1q44";
        if (stage == uint32_t(CainStage::TristramEntered)) return "qstsa1q44";
        if (stage == uint32_t(CainStage::Rescued)) return "qstsa1q46";
        break;
    case QuestId::ForgottenTower:
        if (stage == uint32_t(TowerStage::TomeRead)) return "qstsa1q51";
        if (stage == uint32_t(TowerStage::TowerEntered)) return "qstsa1q51a";
        if (stage == uint32_t(TowerStage::CellarEntered)) return "qstsa1q52";
        break;
    case QuestId::ToolsOfTheTrade:
        if (stage == uint32_t(ToolsStage::Assigned) ||
            stage == uint32_t(ToolsStage::BarracksEntered) ||
            stage == uint32_t(ToolsStage::MalusDropped)) return "qstsa1q31";
        if (stage == uint32_t(ToolsStage::MalusAcquired)) return "qstsa1q32";
        if (stage == uint32_t(ToolsStage::RewardReady)) return "qstsa1q32b";
        break;
    case QuestId::SistersToTheSlaughter:
        if (stage == uint32_t(SlaughterStage::Assigned)) return "qstsa1q61";
        if (stage == uint32_t(SlaughterStage::CatacombsEntered)) return "qstsa1q62";
        if (stage == uint32_t(SlaughterStage::AndarielSlain) ||
            stage == uint32_t(SlaughterStage::PassageReady)) return "qstsa1q63";
        break;
    default: break;
    }
    return {};
}
} // namespace
QuestLogSelection actOneLog(QuestId id, const QuestRecord &record, const QuestLogFacts &facts) {
    QuestLogSelection result;
    if (record.stage) result.descriptionKey = descriptionKey(id, record.stage);
    if (id == QuestId::DenOfEvil && record.stage == uint32_t(DenStage::Entered)) {
        const auto remaining = facts.denRemaining.value_or(0);
        if (remaining > 0 && remaining <= 5) {
            result.descriptionKey = remaining == 1 ? "qstsa1q140" : "qstsa1q14";
            result.appendDenRemaining = remaining > 1;
        }
    }
    return result;
}
} // namespace d2x

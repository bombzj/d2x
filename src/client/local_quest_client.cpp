#include "client/local_quest_client.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include "content/classic_data.hpp"
#include "content/world/world_catalog.hpp"
#include "world/region.hpp"
#include "world/maze.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
namespace {
constexpr std::array titleKeys = {
    "qstsa1q1", "qstsa1q2", "qstsa1q4", "qstsa1q5", "qstsa1q3", "qstsa1q6",
    "qstsa2q1", "qstsa2q2", "qstsa2q3", "qstsa2q4", "qstsa2q5", "qstsa2q6"};
// Keep the existing original-string mapping; stages stay on the authority side.
std::string_view descriptionKey(ActOneQuest quest, uint32_t stage) {
    switch (quest) {
    case QuestId::SevenTombs:
        return stage == 4 ? "qstsa2q65" : stage == 3 ? "qstsa2q64" : stage == 2 ? "qstsa2q63" : "qstsa2q61";
    case QuestId::Summoner:
        return stage == 2 ? "qstsa2q53" : "qstsa2q51";
    case QuestId::ArcaneSanctuary:
        return stage >= 3 ? "qstsa2q42" : "qstsa2q41";
    case QuestId::TaintedSun:
        return stage == 3 ? "qstsa2q33" : stage == 2 ? "qstsa2q32" : "qstsa2q31a";
    case QuestId::HoradricStaff:
        return stage >= 5 ? "qstsa2q24" : "qstsa2q21";
    case QuestId::RadamentsLair:
        if (stage == uint32_t(RadamentStage::Assigned)) return "qstsa2q11";
        if (stage == uint32_t(RadamentStage::LeftTown)) return "qstsa2q12";
        if (stage == uint32_t(RadamentStage::Slain)) return "qstsa2q13";
        break;
    case ActOneQuest::DenOfEvil:
        if (stage == uint32_t(DenStage::Assigned)) return "qstsa1q11";
        if (stage == uint32_t(DenStage::Entered)) return "qstsa1q12";
        if (stage == uint32_t(DenStage::Cleared)) return "qstsa1q15";
        break;
    case ActOneQuest::SistersBurialGrounds:
        if (stage == uint32_t(BurialStage::Assigned)) return "qstsa1q21";
        if (stage == uint32_t(BurialStage::Entered)) return "qstsa1q22";
        if (stage == uint32_t(BurialStage::BloodRavenSlain)) return "qstsa1q23";
        break;
    case ActOneQuest::SearchForCain:
        if (stage == uint32_t(CainStage::Assigned)) return "qstsa1q41";
        if (stage == uint32_t(CainStage::TreeOpened)) return "qstsa1q41";
        if (stage == uint32_t(CainStage::BarkAcquired)) return "qstsa1q42";
        if (stage == uint32_t(CainStage::ScrollTranslated)) return "qstsa1q43";
        if (stage == uint32_t(CainStage::PortalOpened)) return "qstsa1q44";
        if (stage == uint32_t(CainStage::TristramEntered)) return "qstsa1q44";
        if (stage == uint32_t(CainStage::Rescued)) return "qstsa1q46";
        break;
    case ActOneQuest::ForgottenTower:
        if (stage == uint32_t(TowerStage::TomeRead)) return "qstsa1q51";
        if (stage == uint32_t(TowerStage::TowerEntered)) return "qstsa1q51a";
        if (stage == uint32_t(TowerStage::CellarEntered)) return "qstsa1q52";
        break;
    case ActOneQuest::ToolsOfTheTrade:
        if (stage == uint32_t(ToolsStage::Assigned) ||
            stage == uint32_t(ToolsStage::BarracksEntered) ||
            stage == uint32_t(ToolsStage::MalusDropped)) return "qstsa1q31";
        if (stage == uint32_t(ToolsStage::MalusAcquired)) return "qstsa1q32";
        if (stage == uint32_t(ToolsStage::RewardReady)) return "qstsa1q32b";
        break;
    case ActOneQuest::SistersToTheSlaughter:
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
const QuestView &LocalQuestClient::read() const {
    if (cached_.revision == session_.viewRevision()) return cached_;
    QuestView view;
    view.revision = session_.viewRevision();
    view.actor = session_.state().player.id;
    view.currentAct = std::min(1, session_.worldContent().levels().at(int(session_.region().definition.id)).act);
    view.tabCount = view.currentAct >= 1 || session_.quest(QuestId::SistersToTheSlaughter).stage >=
        uint32_t(SlaughterStage::Completed) ? 2 : 1;
    view.denRemaining = session_.denMonstersRemaining();
    view.showDenRemaining = session_.quest(QuestId::DenOfEvil).stage == uint32_t(DenStage::Entered);
    const auto &strings = session_.content().actOneQuestStrings;
    const auto journal = session_.quest(QuestId::ArcaneSanctuary).stage;
    for (size_t index = 0; index < view.entries.size(); ++index) {
        auto &entry = view.entries[index];
        const auto id = QuestId(index);
        const auto &record = session_.quest(id);
        entry.active = record.stage > 0;
        entry.completed = record.stage >= questCompletionStage(id);
        const auto title = strings.find(titleKeys[index]);
        entry.title = title == strings.end() ? titleKeys[index] : title->second;
        std::string_view key;
        if (entry.active || (id == QuestId::SevenTombs && journal >= uint32_t(ArcaneStage::JournalRead))) {
            key = entry.completed ? "qstsComplete" : descriptionKey(id, record.stage);
            if (id == QuestId::HoradricStaff && record.stage < 6) {
                const bool cube = session_.carriesQuestItem(session_.content().cubeCode);
                const bool shaft = session_.carriesQuestItem("msf");
                const bool head = session_.carriesQuestItem("vip");
                const bool complete = session_.carriesQuestItem("hst");
                key = complete ? "qstsa2q24" : cube && shaft && head ? "qstsa2q23" :
                    record.flags & staffScrollExplained ? "qstsa2q22" : "qstsa2q25";
            }
            if (id == QuestId::SevenTombs && journal >= uint32_t(ArcaneStage::JournalRead) &&
                record.stage < uint32_t(TombsStage::DurielSlain)) key = "qstsa2q61a";
        }
        const unsigned remaining = id == QuestId::DenOfEvil && view.showDenRemaining ? view.denRemaining.value_or(0) : 0;
        if (remaining > 0 && remaining <= 5) key = remaining == 1 ? "qstsa1q140" : "qstsa1q14";
        if (const auto description = strings.find(key); description != strings.end()) {
            entry.description = description->second;
            if (key == "qstsa1q14") *entry.description += std::to_string(remaining);
            if (key == "qstsa2q61a") entry.tombSymbol = unsigned(actTwoTombs(session_.state().mapSeed)[0] - 66);
        }
    }
    cached_ = std::move(view);
    return cached_;
}
} // namespace d2x

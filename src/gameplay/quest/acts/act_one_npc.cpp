#include "rules.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/forgotten_tower.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/sisters_to_slaughter.hpp"
#include <algorithm>

namespace d2x {
namespace {
bool assignmentAvailable(const DifficultyQuests &records, QuestId id) {
    auto stage = [&](QuestId quest) { return records.at(questIndex(quest)).stage; };
    switch (id) {
    case QuestId::DenOfEvil: return true;
    case QuestId::SistersBurialGrounds: return stage(QuestId::DenOfEvil) >= uint32_t(DenStage::Rewarded);
    case QuestId::SearchForCain: return stage(QuestId::SistersBurialGrounds) >= uint32_t(BurialStage::Rewarded);
    case QuestId::ToolsOfTheTrade: return stage(QuestId::SearchForCain) >= uint32_t(CainStage::Rewarded);
    case QuestId::SistersToTheSlaughter: return stage(QuestId::ToolsOfTheTrade) >= uint32_t(ToolsStage::RewardReady);
    default: return false;
    }
}
std::optional<QuestSpeechRequest> speechFor(QuestId id, const DifficultyQuests &records, const QuestNpcFacts &facts) {
    const auto &record = records.at(questIndex(id));
    const auto stage = record.stage;
    const auto npc = facts.npcClass;
    std::string_view state, fallback;
    switch (id) {
    case QuestId::DenOfEvil:
        if (!stage && npc == "akara") state = "Init";
        else if (stage == uint32_t(DenStage::Assigned)) state = "AfterInit";
        else if (stage == uint32_t(DenStage::Entered)) { state = "EarlyReturn"; fallback = "AfterInit"; }
        else if (stage == uint32_t(DenStage::Cleared)) state = "Successful";
        break;
    case QuestId::SistersBurialGrounds:
        if (!stage && npc == "kashya" && assignmentAvailable(records, id)) state = "Init";
        else if (stage == uint32_t(BurialStage::Assigned)) state = "AfterInit";
        else if (stage == uint32_t(BurialStage::Entered)) { state = "EarlyReturn"; fallback = "AfterInit"; }
        else if (stage == uint32_t(BurialStage::BloodRavenSlain)) state = "Successful";
        break;
    case QuestId::SearchForCain:
        if (!stage && npc == "akara" && assignmentAvailable(records, id)) state = "Init";
        else if (stage == uint32_t(CainStage::Assigned) || stage == uint32_t(CainStage::TreeOpened)) state = "EarlyReturnS";
        else if (stage == uint32_t(CainStage::BarkAcquired)) state = "AfterInitScroll";
        else if (stage == uint32_t(CainStage::ScrollTranslated)) state = npc == "akara" ? "Instructions" : "SuccessfulScroll";
        else if (stage == uint32_t(CainStage::PortalOpened) || stage == uint32_t(CainStage::TristramEntered)) state = "EarlyReturn";
        else if (stage == uint32_t(CainStage::Rescued)) state = "QuestSuccessful";
        else if (stage == uint32_t(CainStage::Rewarded) && npc.starts_with("cain"))
            state = record.flags & cainRescuedByRogues ? "RescuedByRogues" : "RescuedByHero";
        break;
    case QuestId::ForgottenTower:
        if (stage) state = stage == uint32_t(TowerStage::CountessSlain) ? "Successful"
            : stage >= uint32_t(TowerStage::CellarEntered) ? "EarlyReturn" : "AfterInit";
        break;
    case QuestId::ToolsOfTheTrade:
        if (!stage && npc == "charsi" && assignmentAvailable(records, id)) state = "Init";
        else if (stage) state = stage >= uint32_t(ToolsStage::MalusAcquired) ? "Successful" : "AfterInit";
        break;
    case QuestId::SistersToTheSlaughter:
        if (!stage && npc.starts_with("cain") && assignmentAvailable(records, id)) state = "Init";
        else if (stage) state = stage >= uint32_t(SlaughterStage::AndarielSlain) ? "Successful"
            : stage >= uint32_t(SlaughterStage::CatacombsEntered) ? "EarlyReturn" : "AfterInit";
        break;
    default: break;
    }
    return state.empty() ? std::nullopt : std::optional{QuestSpeechRequest{id, state, fallback}};
}
} // namespace

QuestNpcQuery actOneNpc(const DifficultyQuests &records, const QuestNpcFacts &facts) {
    QuestNpcQuery result;
    const auto npc = facts.npcClass;
    const auto den = records.at(questIndex(QuestId::DenOfEvil)).stage;
    const auto burial = records.at(questIndex(QuestId::SistersBurialGrounds)).stage;
    const auto &cain = records.at(questIndex(QuestId::SearchForCain));
    const auto tools = records.at(questIndex(QuestId::ToolsOfTheTrade)).stage;
    const auto slaughter = records.at(questIndex(QuestId::SistersToTheSlaughter)).stage;
    result.introductionAlert = !facts.introduced && facts.hasIntroduction &&
        (npc == "warriv1" || (npc == "akara" && den < uint32_t(DenStage::Rewarded)));
    for (const auto &definition : questsForAct(0))
        if (const auto request = speechFor(definition.id, records, facts)) result.topics.push_back(*request);
    // Independent unread reactions retain their original acknowledgement keys.
    if (npc.starts_with("cain") && cain.stage >= uint32_t(CainStage::Rescued) && !(cain.flags & cainRescuedByRogues))
        result.dialogues.push_back({{QuestId::SearchForCain, "RescuedByHero", {}}, true, false, true, true});
    std::optional<QuestId> advancing;
    if (npc == "akara") {
        if (cain.stage == uint32_t(CainStage::BarkAcquired) || cain.stage == uint32_t(CainStage::Rescued) ||
            (!cain.stage && assignmentAvailable(records, QuestId::SearchForCain))) advancing = QuestId::SearchForCain;
        else if (!den || den == uint32_t(DenStage::Cleared)) advancing = QuestId::DenOfEvil;
    } else if (npc == "kashya" && (burial == uint32_t(BurialStage::BloodRavenSlain) ||
            (!burial && assignmentAvailable(records, QuestId::SistersBurialGrounds)))) advancing = QuestId::SistersBurialGrounds;
    else if (npc == "charsi" && (tools == uint32_t(ToolsStage::MalusAcquired) ||
            (!tools && assignmentAvailable(records, QuestId::ToolsOfTheTrade)))) advancing = QuestId::ToolsOfTheTrade;
    else if (npc.starts_with("cain") && !slaughter && assignmentAvailable(records, QuestId::SistersToTheSlaughter))
        advancing = QuestId::SistersToTheSlaughter;
    else if (npc == "warriv1" && slaughter == uint32_t(SlaughterStage::AndarielSlain)) advancing = QuestId::SistersToTheSlaughter;
    if (advancing)
        if (const auto speech = speechFor(*advancing, records, facts)) {
            const bool eligible = !(*advancing == QuestId::SearchForCain && cain.stage == uint32_t(CainStage::BarkAcquired) && !facts.items.bark) &&
                !(*advancing == QuestId::ToolsOfTheTrade && tools == uint32_t(ToolsStage::MalusAcquired) &&
                    (facts.characterLevel < 8 || !facts.items.malus));
            result.dialogues.push_back({*speech, eligible, eligible, eligible});
        }
    if (slaughter >= uint32_t(SlaughterStage::AndarielSlain) &&
        (npc.starts_with("cain") || npc == "akara" || npc == "kashya"))
        result.dialogues.push_back({{QuestId::SistersToTheSlaughter, "Successful", {}}, true, false, true, true});
    for (auto it = result.topics.rbegin(); it != result.topics.rend(); ++it) result.dialogues.push_back({*it});
    return result;
}
} // namespace d2x

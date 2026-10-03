#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
namespace {
bool assignmentAvailable(const GameSession &session, ActOneQuest quest) {
    // Single-player projection of D2MOO's Act I SeqCallback chain. A later
    // quest can still start through entering its area or finding its item.
    // Charsi's level-8 check is for taking/returning the Malus, not her briefing.
    switch (quest) {
    case ActOneQuest::DenOfEvil: return true;
    case ActOneQuest::SistersBurialGrounds:
        return session.quest(ActOneQuest::DenOfEvil).stage >= uint32_t(DenStage::Rewarded);
    case ActOneQuest::SearchForCain:
        return session.quest(ActOneQuest::SistersBurialGrounds).stage >= uint32_t(BurialStage::Rewarded);
    case ActOneQuest::ToolsOfTheTrade:
        return session.quest(ActOneQuest::SearchForCain).stage >= uint32_t(CainStage::Rewarded);
    case ActOneQuest::SistersToTheSlaughter:
        return session.quest(ActOneQuest::ToolsOfTheTrade).stage >= uint32_t(ToolsStage::RewardReady);
    default: return false;
    }
}
const NpcSpeech *denSpeech(const GameSession &session, std::string_view speaker) {
    const auto stage = session.quest(ActOneQuest::DenOfEvil).stage;
    const char *state = nullptr;
    if (stage == uint32_t(DenStage::Unstarted)) {
        if (speaker == "Akara") state = "Init";
    } else if (stage == uint32_t(DenStage::Assigned)) state = "AfterInit";
    else if (stage == uint32_t(DenStage::Entered)) state = "EarlyReturn";
    else if (stage == uint32_t(DenStage::Cleared)) state = "Successful";
    if (!state) return nullptr;
    auto &dialogues = session.content().npcDialogues;
    auto result = questSpeech(dialogues, "A1Q1", state, speaker);
    if (!result && stage == uint32_t(DenStage::Entered))
        result = questSpeech(dialogues, "A1Q1", "AfterInit", speaker);
    return result;
}
const NpcSpeech *burialSpeech(const GameSession &session, std::string_view speaker) {
    const auto stage = session.quest(ActOneQuest::SistersBurialGrounds).stage;
    const char *state = nullptr;
    if (stage == uint32_t(BurialStage::Unstarted)) {
        if (speaker == "Kashya" && assignmentAvailable(session, ActOneQuest::SistersBurialGrounds))
            state = "Init";
    } else if (stage == uint32_t(BurialStage::Assigned)) state = "AfterInit";
    else if (stage == uint32_t(BurialStage::Entered)) state = "EarlyReturn";
    else if (stage == uint32_t(BurialStage::BloodRavenSlain)) state = "Successful";
    if (!state) return nullptr;
    auto &dialogues = session.content().npcDialogues;
    auto result = questSpeech(dialogues, "A1Q2", state, speaker);
    if (!result && stage == uint32_t(BurialStage::Entered))
        result = questSpeech(dialogues, "A1Q2", "AfterInit", speaker);
    return result;
}
const NpcSpeech *cainSpeech(const GameSession &session, std::string_view speaker) {
    const auto stage = session.quest(ActOneQuest::SearchForCain).stage;
    const char *state = nullptr;
    if (stage == uint32_t(CainStage::Unstarted)) {
        if (speaker == "Akara" && assignmentAvailable(session, ActOneQuest::SearchForCain))
            state = "Init";
    } else if (stage == uint32_t(CainStage::Assigned) ||
               stage == uint32_t(CainStage::TreeOpened)) state = "EarlyReturnS";
    else if (stage == uint32_t(CainStage::BarkAcquired)) state = "AfterInitScroll";
    else if (stage == uint32_t(CainStage::ScrollTranslated))
        state = speaker == "Akara" ? "Instructions" : "SuccessfulScroll";
    else if (stage == uint32_t(CainStage::PortalOpened) ||
             stage == uint32_t(CainStage::TristramEntered)) state = "EarlyReturn";
    else if (stage == uint32_t(CainStage::Rescued)) state = "QuestSuccessful";
    else if (stage == uint32_t(CainStage::Rewarded) && speaker == "Deckard Cain")
        state = session.quest(ActOneQuest::SearchForCain).flags & cainRescuedByRogues
                    ? "RescuedByRogues" : "RescuedByHero";
    return state ? questSpeech(session.content().npcDialogues, "A1Q4", state, speaker) : nullptr;
}
const NpcSpeech *towerSpeech(const GameSession &session, std::string_view speaker) {
    const auto stage = session.quest(ActOneQuest::ForgottenTower).stage;
    if (stage == uint32_t(TowerStage::Unstarted)) return nullptr;
    const char *state = stage == uint32_t(TowerStage::CountessSlain) ? "Successful"
                        : stage >= uint32_t(TowerStage::CellarEntered) ? "EarlyReturn"
                        : "AfterInit";
    return questSpeech(session.content().npcDialogues, "A1Q5", state, speaker);
}
const NpcSpeech *toolsSpeech(const GameSession &session, std::string_view speaker) {
    const auto stage = session.quest(ActOneQuest::ToolsOfTheTrade).stage;
    if (stage == uint32_t(ToolsStage::Unstarted) &&
        speaker == "Charsi" && assignmentAvailable(session, ActOneQuest::ToolsOfTheTrade))
        return questSpeech(session.content().npcDialogues, "A1Q3", "Init", speaker);
    if (stage == uint32_t(ToolsStage::Unstarted)) return nullptr;
    const char *state = stage >= uint32_t(ToolsStage::MalusAcquired) ? "Successful" : "AfterInit";
    return questSpeech(session.content().npcDialogues, "A1Q3", state, speaker);
}
const NpcSpeech *slaughterSpeech(const GameSession &session, std::string_view speaker) {
    const auto stage = session.quest(ActOneQuest::SistersToTheSlaughter).stage;
    if (stage == uint32_t(SlaughterStage::Unstarted) && speaker == "Deckard Cain" &&
        assignmentAvailable(session, ActOneQuest::SistersToTheSlaughter))
        return questSpeech(session.content().npcDialogues, "A1Q6", "Init", speaker);
    if (stage == uint32_t(SlaughterStage::Unstarted)) return nullptr;
    const char *state = stage >= uint32_t(SlaughterStage::AndarielSlain) ? "Successful"
                        : stage >= uint32_t(SlaughterStage::CatacombsEntered) ? "EarlyReturn"
                        : "AfterInit";
    return questSpeech(session.content().npcDialogues, "A1Q6", state, speaker);
}
} // namespace

std::vector<std::pair<ActOneQuest, const NpcSpeech *>>
GameSession::npcQuestTopics(std::string_view speaker) const {
    // QUESTS_InitScrollTextChain appends each active quest's NPC messages;
    // a Talk menu must retain all of these, rather than only the newest quest.
    std::vector<std::pair<ActOneQuest, const NpcSpeech *>> result;
    if (worldContent_.levels().at(int(region().definition.id)).act == 1) {
        if (int(region().definition.id) == 73) {
            if (quest(QuestId::SevenTombs).stage >= 2)
                if (const auto *speech = introSpeech(content_.npcDialogues, speaker, {}, 1))
                    result.emplace_back(QuestId::SevenTombs, speech);
            return result;
        }
        const auto stage = quest(QuestId::RadamentsLair).stage;
        const auto state = stage == 0 ? "Init" : stage == 1 ? "AfterInit"
            : stage == 2 ? "EarlyReturn" : "Successful";
        if (stage < uint32_t(RadamentStage::Rewarded))
            if (const auto *speech = questSpeech(content_.npcDialogues, "A2Q1", state, speaker))
                result.emplace_back(QuestId::RadamentsLair, speech);
        if (quest(QuestId::HoradricStaff).stage < 6) {
            const auto staffState = carriesQuestItem("hst") ? "SuccessfulStaff" : carriesQuestItem("vip") ? "EarlyReturnCap"
                : carriesQuestItem("msf") ? "EarlyReturnStave" : carriesQuestItem("tr1") ? "EarlyReturnScroll"
                : carriesQuestItem(content_.cubeCode) ? "EarlyReturnCube" : nullptr;
            if (staffState)
                if (const auto *speech = questSpeech(content_.npcDialogues, "A2Q2", staffState, speaker))
                    result.emplace_back(QuestId::HoradricStaff, speech);
        }
        const auto sun = quest(QuestId::TaintedSun).stage;
        if (sun > 0 && sun < 4)
            if (const auto *speech = questSpeech(content_.npcDialogues, "A2Q3", sun == 3 ? "Successful" : sun == 1 ? "AfterInit" : "EarlyReturn", speaker))
                result.emplace_back(QuestId::TaintedSun, speech);
        const auto arcane = quest(QuestId::ArcaneSanctuary).stage;
        const auto summoner = quest(QuestId::Summoner).stage;
        if (arcane >= 3 && summoner < 3)
            if (const auto *speech = questSpeech(content_.npcDialogues, "A2Q5", summoner == 2 ? "Successful" : "EarlyReturn", speaker))
                result.emplace_back(QuestId::Summoner, speech);
        if (sun >= 3 || arcane > 0)
            if (const auto *speech = questSpeech(content_.npcDialogues, "A2Q4", arcane == 0 ? "Init" : arcane < 3 ? "AfterInit" : arcane == 3 ? "EarlyReturn" : "Successful", speaker))
                result.emplace_back(QuestId::ArcaneSanctuary, speech);
        const auto tombs = quest(QuestId::SevenTombs).stage;
        if (arcane > 0 || tombs > 0)
            if (const auto *speech = questSpeech(content_.npcDialogues, "A2Q6", tombs == 0 ? "Init" : tombs >= 3 ? "Successful" : "AfterInit", speaker))
                result.emplace_back(QuestId::SevenTombs, speech);
        return result;
    }
    if (worldContent_.levels().at(int(region().definition.id)).act != 0) return result;
    for (auto [id, speech] : {
             std::pair{ActOneQuest::DenOfEvil, denSpeech(*this, speaker)},
             std::pair{ActOneQuest::SistersBurialGrounds, burialSpeech(*this, speaker)},
             std::pair{ActOneQuest::SearchForCain, cainSpeech(*this, speaker)},
             std::pair{ActOneQuest::ForgottenTower, towerSpeech(*this, speaker)},
             std::pair{ActOneQuest::ToolsOfTheTrade, toolsSpeech(*this, speaker)},
             std::pair{ActOneQuest::SistersToTheSlaughter, slaughterSpeech(*this, speaker)}})
        if (speech) result.emplace_back(id, speech);
    return result;
}

NpcQuestDialogue GameSession::npcQuestDialogue(std::string_view speaker) const {
    if (worldContent_.levels().at(int(region().definition.id)).act == 1) {
        for (auto [id, speech] : npcQuestTopics(speaker)) {
            const auto stage = quest(id).stage;
            const auto identity = content_.npcDialogues.speakers.find(npcIntroductionKey(speaker, 1));
            const bool advances = identity != content_.npcDialogues.speakers.end() &&
                ((id == QuestId::RadamentsLair && identity->second == "atma" && (stage == 0 || stage == uint32_t(RadamentStage::Slain))) ||
                 (id == QuestId::HoradricStaff && speech->state == "EarlyReturnScroll" && !(quest(id).flags & 1)));
            const bool ending = id == QuestId::SevenTombs && identity != content_.npcDialogues.speakers.end() &&
                ((stage == 0 && identity->second == "jerhyn") || (stage == 2 && identity->second == "tyreal") ||
                 (stage == 2 && identity->second == "tyrael") || (stage == 3 && identity->second == "jerhyn") ||
                 (stage == 4 && identity->second == "meshif"));
            const bool progresses = advances || ending || (id == QuestId::Summoner && stage == 2) || (id == QuestId::TaintedSun && (stage == 3 || (stage == 1 && identity != content_.npcDialogues.speakers.end() && identity->second == "drognan"))) ||
                (id == QuestId::ArcaneSanctuary && identity != content_.npcDialogues.speakers.end() &&
                 ((stage == 0 && identity->second == "drognan") || (stage == 1 && identity->second == "jerhyn")));
            if (progresses) return {speech, id, true, {}};
        }
        const auto topics = npcQuestTopics(speaker);
        if (!topics.empty()) return {topics.back().second, std::nullopt, false, {}};
        return {};
    }
    if (worldContent_.levels().at(int(region().definition.id)).act != 0) return {};
    // The speech and the command use the same selected quest. In particular, an
    // unrelated completed quest must never mask Akara's scroll/reward dialogue.
    std::optional<ActOneQuest> advancing;
    const auto den = quest(ActOneQuest::DenOfEvil).stage;
    const auto burial = quest(ActOneQuest::SistersBurialGrounds).stage;
    const auto cain = quest(ActOneQuest::SearchForCain).stage;
    const auto tools = quest(ActOneQuest::ToolsOfTheTrade).stage;
    const auto slaughter = quest(ActOneQuest::SistersToTheSlaughter).stage;
    auto unread = [&](std::string_view questId, std::string_view speechState) -> NpcQuestDialogue {
        const auto *speech = questSpeech(content_.npcDialogues, questId, speechState, speaker);
        auto key = std::string(questId) + "/" + std::string(speechState) + "/" + std::string(speaker);
        if (!speech || !pendingNpcQuestMessages_.contains(key))
            return {};
        return {speech, std::nullopt, true, std::move(key)};
    };
    // A1Q4 keeps Cain's thanks separate from Akara's ring reward.
    if (speaker == "Deckard Cain" && cain >= uint32_t(CainStage::Rescued) &&
        !(quest(ActOneQuest::SearchForCain).flags & cainRescuedByRogues))
        if (auto thanks = unread("A1Q4", "RescuedByHero"); thanks.speech) return thanks;
    if (speaker == "Akara") {
        if (cain == uint32_t(CainStage::BarkAcquired) ||
            cain == uint32_t(CainStage::Rescued) ||
            (cain == uint32_t(CainStage::Unstarted) &&
             assignmentAvailable(*this, ActOneQuest::SearchForCain)))
            advancing = ActOneQuest::SearchForCain;
        else if (den == uint32_t(DenStage::Unstarted) || den == uint32_t(DenStage::Cleared))
            advancing = ActOneQuest::DenOfEvil;
    } else if (speaker == "Kashya" &&
               (burial == uint32_t(BurialStage::BloodRavenSlain) ||
                (burial == uint32_t(BurialStage::Unstarted) &&
                 assignmentAvailable(*this, ActOneQuest::SistersBurialGrounds))))
        advancing = ActOneQuest::SistersBurialGrounds;
    else if (speaker == "Charsi" &&
             (tools == uint32_t(ToolsStage::MalusAcquired) ||
              (tools == uint32_t(ToolsStage::Unstarted) &&
               assignmentAvailable(*this, ActOneQuest::ToolsOfTheTrade))))
        advancing = ActOneQuest::ToolsOfTheTrade;
    else if (speaker == "Deckard Cain" && slaughter == uint32_t(SlaughterStage::Unstarted) &&
             assignmentAvailable(*this, ActOneQuest::SistersToTheSlaughter))
        advancing = ActOneQuest::SistersToTheSlaughter;
    else if (speaker == "Warriv" && slaughter == uint32_t(SlaughterStage::AndarielSlain))
        advancing = ActOneQuest::SistersToTheSlaughter;

    auto speechFor = [&](ActOneQuest id) -> const NpcSpeech * {
        switch (id) {
        case ActOneQuest::DenOfEvil: return denSpeech(*this, speaker);
        case ActOneQuest::SistersBurialGrounds: return burialSpeech(*this, speaker);
        case ActOneQuest::SearchForCain: return cainSpeech(*this, speaker);
        case ActOneQuest::ForgottenTower: return towerSpeech(*this, speaker);
        case ActOneQuest::ToolsOfTheTrade: return toolsSpeech(*this, speaker);
        case ActOneQuest::SistersToTheSlaughter: return slaughterSpeech(*this, speaker);
        default: return nullptr;
        }
    };
    if (advancing) {
        const auto *speech = speechFor(*advancing);
        auto carried = [&](std::string_view code) {
            for (auto container : {playerContainers_.backpack, playerContainers_.equipment})
                for (auto item : inventory_.contents(container))
                    if (inventory_.item(item)->definition == code) return true;
            return false;
        };
        const bool hasItem = !(*advancing == ActOneQuest::SearchForCain &&
                              cain == uint32_t(CainStage::BarkAcquired) && !carried("bks")) &&
                             !(*advancing == ActOneQuest::ToolsOfTheTrade &&
                              tools == uint32_t(ToolsStage::MalusAcquired) &&
                              (state().player.level < 8 || !carried("hdm")));
        return {speech, speech && hasItem ? advancing : std::nullopt, speech && hasItem, {}};
    }
    // A1Q6's three separate NPC reaction lists are acknowledged independently.
    if (slaughter >= uint32_t(SlaughterStage::AndarielSlain) &&
        (speaker == "Deckard Cain" || speaker == "Akara" || speaker == "Kashya"))
        if (auto reaction = unread("A1Q6", "Successful"); reaction.speech) return reaction;
    for (const auto id : {ActOneQuest::SistersToTheSlaughter, ActOneQuest::ToolsOfTheTrade,
                          ActOneQuest::ForgottenTower, ActOneQuest::SearchForCain,
                          ActOneQuest::SistersBurialGrounds, ActOneQuest::DenOfEvil})
        if (const auto *speech = speechFor(id)) return {speech, std::nullopt, false, {}};
    return {};
}

bool GameSession::npcQuestAlert(const WorldObject &npc) const {
    if (npc.questHidden || npc.npcClass.empty() || engagedNpc_ == npc.id ||
        !region().definition.safe) return false;
    const auto &introductions = state().player.npcIntroductions.at(size_t(state().population.difficulty));
    // ACT1Intro only marks Akara; A1Q0 marks Warriv. Other introductions have no alert.
    if (npc.act == 0 && !introductions.contains(npc.name) &&
        introSpeech(content_.npcDialogues, npc.name, state().player.characterClass) &&
        (npc.name == "Warriv" || (npc.name == "Akara" &&
         quest(ActOneQuest::DenOfEvil).stage < uint32_t(DenStage::Rewarded)))) return true;
    return npcQuestDialogue(npc.name).automatic;
}

std::optional<unsigned> GameSession::denMonstersRemaining() const {
    if (!denRegion_) return {};
    const AreaState *den = state().area.region == *denRegion_ ? &state().area : nullptr;
    if (!den)
        for (const auto &area : inactiveAreas_)
            if (area.region == *denRegion_) { den = &area; break; }
    if (!den || !den->initialized) return {};
    return unsigned(den->pendingSpawns.size()) + unsigned(std::count_if(
        den->enemies.begin(), den->enemies.end(), [](const Enemy &enemy) { return enemy.hp > 0; }));
}
} // namespace d2x

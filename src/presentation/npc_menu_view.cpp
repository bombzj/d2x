#include "scene_view.hpp"
#include "content/npc_dialogue.hpp"
#include <algorithm>

namespace d2x {
namespace {
struct MenuEntry { const char *label; int action; };
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
        if (speaker == "Kashya" && session.quest(ActOneQuest::DenOfEvil).stage >=
            uint32_t(DenStage::Rewarded)) state = "Init";
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
        if (speaker == "Akara" && session.quest(ActOneQuest::SistersBurialGrounds).stage >=
            uint32_t(BurialStage::BloodRavenSlain)) state = "Init";
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
        speaker == "Charsi" && session.state().player.level >= 8)
        return questSpeech(session.content().npcDialogues, "A1Q3", "Init", speaker);
    if (stage == uint32_t(ToolsStage::Unstarted)) return nullptr;
    const char *state = stage >= uint32_t(ToolsStage::MalusAcquired) ? "Successful" : "AfterInit";
    return questSpeech(session.content().npcDialogues, "A1Q3", state, speaker);
}
const NpcSpeech *slaughterSpeech(const GameSession &session, std::string_view speaker) {
    const auto stage = session.quest(ActOneQuest::SistersToTheSlaughter).stage;
    if (stage == uint32_t(SlaughterStage::Unstarted) && speaker == "Deckard Cain" &&
        session.quest(ActOneQuest::SearchForCain).stage >= uint32_t(CainStage::Rescued))
        return questSpeech(session.content().npcDialogues, "A1Q6", "Init", speaker);
    if (stage == uint32_t(SlaughterStage::Unstarted)) return nullptr;
    const char *state = stage >= uint32_t(SlaughterStage::AndarielSlain) ? "Successful"
                        : stage >= uint32_t(SlaughterStage::CatacombsEntered) ? "EarlyReturn"
                        : "AfterInit";
    return questSpeech(session.content().npcDialogues, "A1Q6", state, speaker);
}
const NpcSpeech *actQuestSpeech(const GameSession &session, std::string_view speaker) {
    if (auto speech = slaughterSpeech(session, speaker)) return speech;
    if (auto speech = toolsSpeech(session, speaker)) return speech;
    if (auto speech = towerSpeech(session, speaker)) return speech;
    if (auto speech = cainSpeech(session, speaker)) return speech;
    if (auto speech = burialSpeech(session, speaker)) return speech;
    return denSpeech(session, speaker);
}
std::vector<MenuEntry> entries(const GameSession &session, EntityId npc, std::string_view speaker) {
    std::vector<MenuEntry> result;
    if (actQuestSpeech(session, speaker) || introSpeech(session.content().npcDialogues, speaker))
        result.push_back({"Talk", 1});
    if (session.vendorStock(npc))
        result.push_back({"Trade", 2});
    if (speaker == "Deckard Cain")
        result.push_back({"Identify Items", 3});
    if (speaker == "Akara") {
        const auto &quest = session.quest(ActOneQuest::DenOfEvil);
        if (quest.stage == uint32_t(DenStage::Rewarded) && !(quest.flags & denRespecUsed))
            result.push_back({"Reset Stat/Skill Points", 6});
    }
    if (speaker == "Charsi" && session.quest(ActOneQuest::ToolsOfTheTrade).stage ==
                                    uint32_t(ToolsStage::RewardReady))
        result.push_back({"Imbue", 7});
    if (speaker == "Warriv" && session.quest(ActOneQuest::SistersToTheSlaughter).stage ==
                                   uint32_t(SlaughterStage::PassageReady))
        result.push_back({"Go East", 8});
    if (gossipSpeech(session.content().npcDialogues, speaker, 0))
        result.push_back({"Gossip", 5});
    result.push_back({"Cancel", 4});
    return result;
}
Rectangle menuBounds(const SceneView &view, const GameSession &session, EntityId npc,
                     std::string_view speaker, const std::vector<MenuEntry> &options) {
    const auto *object = session.object(npc);
    Vec point = object ? view.screen(object->pos) : Vec{W / 2.f, H / 2.f};
    size_t longest = speaker.size();
    for (const auto &entry : options)
        longest = std::max(longest, std::string_view(entry.label).size());
    float width = std::clamp(18.f + float(longest) * 9.f, 94.f, 180.f);
    float height = 27.f + float(options.size()) * 20.f;
    return {std::clamp(point.x - width / 2, 8.f, float(W) - width - 8.f),
            std::clamp(point.y - height - 43.f, 8.f, float(H - HUD) - height - 8.f),
            width, height};
}
} // namespace
void SceneView::openNpcMenu(EntityId object, std::string speaker) {
    view_.dialogueObject = object;
    view_.dialogueSpeaker = std::move(speaker);
    view_.dialogue.clear();
    view_.dialogueLines.clear();
    view_.dialogueStatus.clear();
    view_.shopOpen = false;
    view_.npcMenu = true;
    view_.inventory.open = false;
    view_.dialogueGossipTurn = view_.dialogueSpeaker == "Deckard Cain" ? 1 : 0;
}
bool SceneView::startNpcTalk() {
    const auto *speech = actQuestSpeech(session_, view_.dialogueSpeaker);
    if (!speech)
        speech = introSpeech(session_.content().npcDialogues, view_.dialogueSpeaker);
    if (!speech) return false;
    auto npc = view_.dialogueObject;
    auto speaker = view_.dialogueSpeaker;
    openNpcDialogue(npc, std::move(speaker), speech->text);
    view_.dialogueGossipTurn = view_.dialogueSpeaker == "Deckard Cain" ? 1 : 0;
    return true;
}
int SceneView::clickNpcMenu(Vec mouse) {
    auto options = entries(session_, view_.dialogueObject, view_.dialogueSpeaker);
    auto bounds = menuBounds(*this, session_, view_.dialogueObject, view_.dialogueSpeaker, options);
    for (int row = 0; row < int(options.size()); ++row) {
        Rectangle choice{bounds.x + 5, bounds.y + 24 + row * 20.f, bounds.width - 10, 20};
        if (CheckCollisionPointRec(rv(mouse), choice)) return options[size_t(row)].action;
    }
    return 0;
}
void SceneView::drawNpcMenu() const {
    auto options = entries(session_, view_.dialogueObject, view_.dialogueSpeaker);
    auto bounds = menuBounds(*this, session_, view_.dialogueObject, view_.dialogueSpeaker, options);
    DrawRectangleRec(bounds, {0, 0, 0, 222});
    DrawRectangleLinesEx(bounds, 1, gold);
    painter_.label(view_.dialogueSpeaker, int(bounds.x) + 9, int(bounds.y) + 5, 14, gold);
    for (int row = 0; row < int(options.size()); ++row)
        painter_.label(options[size_t(row)].label, int(bounds.x) + 11,
                       int(bounds.y) + 25 + row * 20, 14, parchment);
    if (!view_.dialogueStatus.empty())
        painter_.label(view_.dialogueStatus, int(bounds.x), int(bounds.y + bounds.height + 5), 12, gold);
}
} // namespace d2x

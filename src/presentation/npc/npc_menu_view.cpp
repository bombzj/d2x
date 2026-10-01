#include "gameplay/session/session.hpp"
#include "presentation/scene_view.hpp"
#include "content/npc/npc_dialogue.hpp"
#include <algorithm>
#include <cctype>

namespace d2x {
namespace {
struct MenuEntry { std::string label; int action; };
std::vector<MenuEntry> entries(const GameSession &session, EntityId npc, std::string_view speaker,
                               bool topics) {
    std::vector<MenuEntry> result;
    const auto *npcObject = session.object(npc);
    const std::string_view npcClass = npcObject ? std::string_view(npcObject->npcClass) : std::string_view{};
    if (topics) {
        if (introSpeech(session.content().npcDialogues, speaker,
                        session.state().player.characterClass))
            result.push_back({"Introduction", 11});
        for (auto [id, speech] : session.npcQuestTopics(speaker)) {
            std::string key = "qsts" + speech->quest;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                return char(std::tolower(c));
            });
            if (auto title = session.content().actOneQuestStrings.find(key);
                title != session.content().actOneQuestStrings.end())
                result.push_back({title->second, 100 + int(questIndex(id))});
        }
        if (gossipSpeech(session.content().npcDialogues, speaker, 0))
            result.push_back({"Gossip", 5});
        result.push_back({"Cancel", 4});
        return result;
    }
    if (introSpeech(session.content().npcDialogues, speaker, session.state().player.characterClass) ||
        !session.npcQuestTopics(speaker).empty() || gossipSpeech(session.content().npcDialogues, speaker, 0))
        result.push_back({"Talk", 1});
    if (session.vendorStock(npc)) {
        result.push_back({npcCanRepair(npcClass) ? "Trade / Repair" : "Trade", 2});
    }
    if (npcCanGamble(npcClass))
        result.push_back({"Gamble", 9});
    if (session.canResurrectHireling(npc))
        result.push_back({"Resurrect: " + std::to_string(session.hirelingResurrectionCost()), 12});
    if (session.canHireFrom(npc))
        result.push_back({"Hire", 10});
    if (npcClass.starts_with("cain"))
        result.push_back({"Identify Items", 3});
    if (npcClass == "akara") {
        const auto &quest = session.quest(ActOneQuest::DenOfEvil);
        if (quest.stage == uint32_t(DenStage::Rewarded) && !(quest.flags & denRespecUsed))
            result.push_back({"Reset Stat/Skill Points", 6});
    }
    if (npcClass == "charsi" && session.quest(ActOneQuest::ToolsOfTheTrade).stage ==
                                    uint32_t(ToolsStage::RewardReady))
        result.push_back({"Imbue", 7});
    if (npcClass == "warriv1" && session.quest(ActOneQuest::SistersToTheSlaughter).stage ==
                                   uint32_t(SlaughterStage::PassageReady))
        result.push_back({"Go East", 8});
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
    float width = std::clamp(18.f + float(longest) * 9.f, 94.f, 340.f);
    float height = 27.f + float(options.size()) * 20.f;
    return {std::clamp(point.x - width / 2, 8.f, float(W) - width - 8.f),
            std::clamp(point.y - height - 43.f, 8.f, float(H - HUD) - height - 8.f),
            width, height};
}
} // namespace
void SceneView::openNpcMenu(EntityId object, std::string speaker, bool firstIntroduction) {
    // A delayed interaction event must not reopen the menu over an active NPC view.
    if (view_.shopOpen || view_.hireListOpen || !view_.dialogue.empty()) return;
    if (firstIntroduction) {
        if (auto intro = introSpeech(session_.content().npcDialogues, speaker,
                                      session_.state().player.characterClass)) {
            openNpcDialogue(object, std::move(speaker), intro->text);
            return;
        }
    }
    view_.dialogueObject = object;
    view_.dialogueSpeaker = std::move(speaker);
    cancelNpcDialogue();
    view_.shopOpen = false;
    view_.npcMenu = true;
    view_.npcTopics = false;
    view_.inventory.open = false;
    auto turn = view_.npcGossipTurns.try_emplace(view_.dialogueSpeaker, 0u);
    view_.dialogueGossipTurn = turn.first->second;
}
bool SceneView::startNpcTalk() {
    if (!view_.npcMenu) return false;
    view_.npcTopics = true;
    return true;
}
bool SceneView::startNpcIntroduction() {
    if (!view_.npcMenu || !view_.npcTopics) return false;
    const auto *speech = introSpeech(session_.content().npcDialogues, view_.dialogueSpeaker,
                                     session_.state().player.characterClass);
    if (!speech) return false;
    openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, speech->text);
    return true;
}
bool SceneView::startNpcTopic(ActOneQuest quest) {
    if (!view_.npcMenu || !view_.npcTopics) return false;
    for (auto [id, speech] : session_.npcQuestTopics(view_.dialogueSpeaker))
        if (id == quest) {
            // Reviewing a topic is presentation only. Quest transitions are
            // acknowledged by the automatic dialogue on NPC activation.
            openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, speech->text);
            return true;
        }
    return false;
}
int SceneView::clickNpcMenu(Vec mouse) {
    auto options = entries(session_, view_.dialogueObject, view_.dialogueSpeaker, view_.npcTopics);
    auto bounds = menuBounds(*this, session_, view_.dialogueObject, view_.dialogueSpeaker, options);
    for (int row = 0; row < int(options.size()); ++row) {
        Rectangle choice{bounds.x + 5, bounds.y + 24 + row * 20.f, bounds.width - 10, 20};
        if (CheckCollisionPointRec(rv(mouse), choice)) return options[size_t(row)].action;
    }
    return 0;
}
void SceneView::drawNpcMenu(Vec mouse) const {
    auto options = entries(session_, view_.dialogueObject, view_.dialogueSpeaker, view_.npcTopics);
    auto bounds = menuBounds(*this, session_, view_.dialogueObject, view_.dialogueSpeaker, options);
    DrawRectangleRec(bounds, {0, 0, 0, 222});
    DrawRectangleLinesEx(bounds, 1, gold);
    std::string speaker = view_.dialogueSpeaker;
    std::transform(speaker.begin(), speaker.end(), speaker.begin(), [](unsigned char c) {
        return char(std::toupper(c));
    });
    painter_.label(speaker, int(bounds.x) + 9, int(bounds.y) + 5, 14, gold);
    for (int row = 0; row < int(options.size()); ++row) {
        Rectangle choice{bounds.x + 5, bounds.y + 24 + row * 20.f, bounds.width - 10, 20};
        const auto color = CheckCollisionPointRec(rv(mouse), choice)
                               ? Color{94, 112, 200, 255} : parchment;
        painter_.label(options[size_t(row)].label, int(bounds.x) + 11,
                       int(bounds.y) + 25 + row * 20, 14, color);
    }
    if (!view_.dialogueStatus.empty())
        painter_.label(view_.dialogueStatus, int(bounds.x), int(bounds.y + bounds.height + 5), 12, gold);
}
} // namespace d2x

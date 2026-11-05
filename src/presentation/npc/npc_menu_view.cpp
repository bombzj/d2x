#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cctype>
#include "npc_menu.hpp"

namespace d2x {
Rectangle npcMenuBounds(const OriginalMenu &menu, Vec point, Rectangle viewport,
    std::string_view speaker, std::span<const std::string> options) {
    // D2Client 46880 -> A3580: position the service list around the
    // projected unit, then reserve the original top/left margin.
    const float anchor = std::max(viewport.y+20, point.y-150);
    const float top = anchor - int(menu.size(speaker, options).y)/3;
    auto box = menu.bounds({point.x, top}, viewport, speaker, options);
    if (box.x + box.width > viewport.x + viewport.width - 10)
        box.x = viewport.x + viewport.width - box.width;
    if (box.y + box.height > viewport.y + viewport.height - 10)
        box.y = viewport.y + viewport.height - box.height;
    box.x = std::max(viewport.x+10, box.x);
    box.y = std::max(viewport.y+10, box.y);
    return box;
}
namespace {
std::vector<std::string> labels(const std::vector<NpcMenuEntry> &options) {
    std::vector<std::string> result;
    for (const auto &entry : options) result.push_back(entry.label);
    return result;
}
} // namespace
void SceneView::openNpcMenu(EntityId object, std::string speaker, bool firstIntroduction) {
    // A delayed interaction event must not reopen the menu over an active NPC view.
    if (view_.shopOpen || view_.hireListOpen || !view_.dialogue.empty()) return;
    refreshNpcView(object);
    if (firstIntroduction && npcView_.introduction) {
        openNpcDialogue(object, std::move(speaker), *npcView_.introduction);
        return;
    }
    view_.dialogueObject = object;
    view_.dialogueSpeaker = std::move(speaker);
    cancelNpcDialogue();
    view_.shopOpen = false;
    view_.npcMenu = true;
    view_.npcTopics = false;
    if(npcView_.returnGreetingClass && assets_.sceneAudio.npcReturnGreeting(*npcView_.returnGreetingClass))
        npcClient_.submit(AcknowledgeNpcGreeting{object});
    else greetNpc(object);
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
    if (!npcView_.introduction) return false;
    openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, *npcView_.introduction);
    return true;
}
bool SceneView::startNpcTopic(QuestId quest) {
    if (!view_.npcMenu || !view_.npcTopics) return false;
    for (const auto &topic : npcView_.topics)
        if (topic.quest == quest) {
            // Reviewing a topic is presentation only. Quest transitions are
            // acknowledged by the automatic dialogue on NPC activation.
            openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, topic.text);
            return true;
        }
    return false;
}
bool SceneView::startNpcTextTopic(uint32_t topic) {
    const auto found=npcView_.textTopics.find(topic);
    if (!view_.npcMenu || found==npcView_.textTopics.end()) return false;
    openNpcDialogue(view_.dialogueObject,view_.dialogueSpeaker,found->second,topic);
    view_.dialogueTextTopic = topic;
    view_.dialogueFromMenu = true;
    return true;
}
std::optional<NpcMenuSelection> SceneView::clickNpcMenu(Vec mouse) const {
    const auto &options = view_.npcTopics ? npcView_.talkEntries : npcView_.services;
    const auto box = npcMenuBounds(originalMenu_, npcView_.valid ? screen(npcView_.position) : Vec{W/2.f,H/2.f},
        worldViewport(), view_.dialogueSpeaker, labels(options));
    if (!CheckCollisionPointRec(rv(mouse), box)) return {};
    const int row = originalMenu_.hit(box, !view_.dialogueSpeaker.empty(), options.size(), mouse);
    return row < 0 ? NpcMenuSelection{} : options[size_t(row)].selection;
}
void SceneView::drawNpcMenu(Vec mouse) const {
    const auto &options = view_.npcTopics ? npcView_.talkEntries : npcView_.services;
    d2x::drawNpcMenu(originalMenu_, npcView_.valid ? screen(npcView_.position) : Vec{W / 2.f, H / 2.f},
        worldViewport(),view_.dialogueSpeaker, labels(options), mouse, view_.dialogueStatus);
}
void drawNpcMenu(const OriginalMenu &menu, Vec point, Rectangle viewport, std::string_view name,
    std::span<const std::string> options, Vec mouse, std::string_view status) {
    const auto box = npcMenuBounds(menu,point,viewport,name,options);
    menu.draw(box,name,options,menu.hit(box,!name.empty(),options.size(),mouse),status);
}
} // namespace d2x

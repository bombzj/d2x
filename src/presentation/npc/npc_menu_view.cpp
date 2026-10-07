#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cctype>
#include "npc_menu.hpp"
#include "presentation/hud/hud_layout.hpp"

namespace d2x {
Rectangle npcMenuBounds(const OriginalMenu &menu, Vec point, Rectangle viewport,
    std::string_view speaker, std::span<const std::string> options) {
    return menu.bounds({point.x, point.y-menu.size(speaker,options,hudScale).y-43*hudScale},
        viewport,speaker,options,hudScale);
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
    openNpcDialogue(view_.dialogueObject,view_.dialogueSpeaker,found->second);
    view_.dialogueTextTopic = topic;
    return true;
}
NpcMenuSelection SceneView::clickNpcMenu(Vec mouse) {
    const auto &options = view_.npcTopics ? npcView_.talkEntries : npcView_.services;
    const auto row = npcMenuHit(originalMenu_,npcView_.valid ? screen(npcView_.position) : Vec{W/2.f,H/2.f},
        worldViewport(),view_.dialogueSpeaker,labels(options),mouse);
    if (row) return options[*row].selection;
    return {};
}
void SceneView::drawNpcMenu(Vec mouse) const {
    const auto &options = view_.npcTopics ? npcView_.talkEntries : npcView_.services;
    d2x::drawNpcMenu(originalMenu_, npcView_.valid ? screen(npcView_.position) : Vec{W / 2.f, H / 2.f},
        worldViewport(),view_.dialogueSpeaker, labels(options), mouse, view_.dialogueStatus);
}
std::optional<size_t> npcMenuHit(const OriginalMenu &menu, Vec point, Rectangle viewport,
    std::string_view speaker, std::span<const std::string> options, Vec mouse) {
    const auto row = menu.hit(npcMenuBounds(menu,point,viewport,speaker,options),!speaker.empty(),options.size(),hudScale,mouse);
    return row < 0 ? std::nullopt : std::optional<size_t>{size_t(row)};
}
void drawNpcMenu(const OriginalMenu &menu, Vec point, Rectangle viewport, std::string_view name,
    std::span<const std::string> options, Vec mouse, std::string_view status) {
    const auto box = npcMenuBounds(menu,point,viewport,name,options);
    menu.draw(box,name,options,hudScale,menu.hit(box,!name.empty(),options.size(),hudScale,mouse),status);
}
} // namespace d2x

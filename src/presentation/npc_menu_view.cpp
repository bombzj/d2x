#include "scene_view.hpp"
#include "content/npc_dialogue.hpp"
#include <algorithm>

namespace d2x {
namespace {
struct MenuEntry { const char *label; int action; };
std::vector<MenuEntry> entries(const GameSession &session, EntityId npc, std::string_view speaker) {
    std::vector<MenuEntry> result;
    if (introSpeech(session.content().npcDialogues, speaker))
        result.push_back({"TALK", 1});
    if (session.vendorStock(npc))
        result.push_back({"TRADE", 2});
    if (speaker == "Deckard Cain")
        result.push_back({"IDENTIFY ITEMS", 3});
    result.push_back({"CANCEL", 4});
    return result;
}
Rectangle menuBounds(const SceneView &view, const GameSession &session, EntityId npc, int rows) {
    const auto *object = session.object(npc);
    Vec point = object ? view.screen(object->pos) : Vec{W / 2.f, H / 2.f};
    float height = 36.f + rows * 31.f;
    return {std::clamp(point.x - 105.f, 12.f, float(W) - 222.f),
            std::clamp(point.y - height - 65.f, 12.f, float(H - HUD) - height - 12.f),
            210.f, height};
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
}
bool SceneView::startNpcTalk() {
    const auto *speech = introSpeech(session_.content().npcDialogues, view_.dialogueSpeaker);
    if (!speech) return false;
    auto npc = view_.dialogueObject;
    auto speaker = view_.dialogueSpeaker;
    openNpcDialogue(npc, std::move(speaker), speech->text);
    view_.dialogueGossipTurn = view_.dialogueSpeaker == "Deckard Cain" ? 1 : 0;
    return true;
}
int SceneView::clickNpcMenu(Vec mouse) {
    auto options = entries(session_, view_.dialogueObject, view_.dialogueSpeaker);
    auto bounds = menuBounds(*this, session_, view_.dialogueObject, int(options.size()));
    for (int row = 0; row < int(options.size()); ++row) {
        Rectangle choice{bounds.x + 10, bounds.y + 29 + row * 31.f, bounds.width - 20, 29};
        if (CheckCollisionPointRec(rv(mouse), choice)) return options[size_t(row)].action;
    }
    return 0;
}
void SceneView::drawNpcMenu() const {
    auto options = entries(session_, view_.dialogueObject, view_.dialogueSpeaker);
    auto bounds = menuBounds(*this, session_, view_.dialogueObject, int(options.size()));
    frame(bounds);
    painter_.label(view_.dialogueSpeaker, int(bounds.x) + 13, int(bounds.y) + 8, 16, gold);
    for (int row = 0; row < int(options.size()); ++row)
        painter_.label(options[size_t(row)].label, int(bounds.x) + 23,
                       int(bounds.y) + 34 + row * 31, 16, parchment);
    if (!view_.dialogueStatus.empty())
        painter_.label(view_.dialogueStatus, int(bounds.x), int(bounds.y + bounds.height + 5), 12, gold);
}
} // namespace d2x

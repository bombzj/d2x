#include "scene_view.hpp"
#include "content/npc_dialogue.hpp"
#include <algorithm>

namespace d2x {
namespace {
struct MenuEntry { const char *label; int action; };
std::vector<MenuEntry> entries(const GameSession &session, EntityId npc, std::string_view speaker) {
    std::vector<MenuEntry> result;
    if (introSpeech(session.content().npcDialogues, speaker))
        result.push_back({"Talk", 1});
    if (session.vendorStock(npc))
        result.push_back({"Trade", 2});
    if (speaker == "Deckard Cain")
        result.push_back({"Identify Items", 3});
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

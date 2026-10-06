#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cctype>
#include "npc_menu.hpp"

namespace d2x {
Rectangle npcMenuBounds(Vec point, std::string_view speaker, std::span<const std::string> options) {
    size_t longest = speaker.size();
    for (const auto &entry : options)
        longest = std::max(longest, entry.size());
    float width = std::clamp(18.f + float(longest) * 9.f, 94.f, 340.f);
    float height = 27.f + float(options.size()) * 20.f;
    return {std::clamp(point.x - width / 2, 8.f, float(W) - width - 8.f),
            std::clamp(point.y - height - 43.f, 8.f, float(H - HUD) - height - 8.f),
            width, height};
}
namespace {
std::vector<std::string> labels(const std::vector<NpcMenuEntry> &options) {
    std::vector<std::string> result;
    for (const auto &entry : options) result.push_back(entry.label);
    return result;
}
Rectangle menuBounds(const SceneView &view, const NpcConversationView &npc,
    std::string_view speaker, const std::vector<NpcMenuEntry> &options) {
    return npcMenuBounds(npc.valid ? view.screen(npc.position) : Vec{W / 2.f, H / 2.f}, speaker, labels(options));
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
    auto bounds = menuBounds(*this, npcView_, view_.dialogueSpeaker, options);
    for (int row = 0; row < int(options.size()); ++row) {
        Rectangle choice{bounds.x + 5, bounds.y + 24 + row * 20.f, bounds.width - 10, 20};
        if (CheckCollisionPointRec(rv(mouse), choice)) return options[size_t(row)].selection;
    }
    return {};
}
void SceneView::drawNpcMenu(Vec mouse) const {
    const auto &options = view_.npcTopics ? npcView_.talkEntries : npcView_.services;
    d2x::drawNpcMenu(assets_.font, npcView_.valid ? screen(npcView_.position) : Vec{W / 2.f, H / 2.f},
        view_.dialogueSpeaker, labels(options), mouse, view_.dialogueStatus);
}
std::optional<size_t> npcMenuHit(Vec point, std::string_view speaker, std::span<const std::string> options, Vec mouse) {
    const auto bounds = npcMenuBounds(point, speaker, options);
    for (size_t row = 0; row < options.size(); ++row)
        if (CheckCollisionPointRec(rv(mouse), {bounds.x + 5, bounds.y + 24 + row * 20.f,
            bounds.width - 10, 20})) return row;
    return {};
}
void drawNpcMenu(const ClassicFont &font, Vec point, std::string_view name,
    std::span<const std::string> options, Vec mouse, std::string_view status) {
    const auto bounds = npcMenuBounds(point, name, options);
    const UiPainter painter(font);
    DrawRectangleRec(bounds, {0, 0, 0, 222});
    DrawRectangleLinesEx(bounds, 1, gold);
    std::string speaker(name);
    std::transform(speaker.begin(), speaker.end(), speaker.begin(), [](unsigned char c) {
        return char(std::toupper(c));
    });
    painter.label(speaker, int(bounds.x) + 9, int(bounds.y) + 5, 14, gold);
    for (int row = 0; row < int(options.size()); ++row) {
        Rectangle choice{bounds.x + 5, bounds.y + 24 + row * 20.f, bounds.width - 10, 20};
        const auto color = CheckCollisionPointRec(rv(mouse), choice)
                               ? Color{94, 112, 200, 255} : parchment;
        painter.label(options[size_t(row)], int(bounds.x) + 11,
                       int(bounds.y) + 25 + row * 20, 14, color);
    }
    if (!status.empty())
        painter.label(std::string(status), int(bounds.x), int(bounds.y + bounds.height + 5), 12, gold);
}
} // namespace d2x

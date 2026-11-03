#include "presentation/scene_view.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include <algorithm>

namespace d2x {
namespace {
// D2Client 7D200 / 7BB40, in native UI pixels.
constexpr int lineHeight = 18;
constexpr int firstBaseline = 124;
Rectangle speechBounds() { return {float((W-325)/2), 7, 325, 122}; }
float endOfScroll(size_t lines) {
    return 113.f + std::max(1, int(lines)-1) * lineHeight;
}
} // namespace

void SceneView::openNpcDialogue(EntityId object, std::string speaker, std::string text, std::optional<uint32_t> stringId) {
    cancelNpcDialogue();
    displayNpcDialogue(object, std::move(speaker), std::move(text));
    assets_.streams.speak(view_.dialogue, stringId);
}

void SceneView::cancelNpcDialogue() {
    assets_.streams.stopSpeech();
    view_.dialogue.clear();
    view_.dialogueTextTopic.reset();
    view_.dialogueFromMenu = false;
    view_.dialogueLines.clear();
    view_.dialogueStatus.clear();
    view_.dialogueOffset = 0.f;
    view_.dialogueManualScroll = false;
    view_.npcMenu = false;
    view_.npcTopics = false;
}

void SceneView::displayNpcDialogue(EntityId object, std::string speaker, std::string text) {
    view_.dialogueObject = object;
    refreshNpcView(object);
    view_.shopOpen = false;
    view_.shopSalePending.reset();
    view_.npcMenu = false;
    view_.npcTopics = false;
    view_.shopPage = 0;
    view_.dialogueSpeaker = std::move(speaker);
    view_.dialogue = std::move(text);
    view_.dialogueStatus.clear();
    view_.dialogueOffset = 0.f;
    view_.dialogueManualScroll = false;
    view_.dialogueLines.clear();
    std::string_view remaining = view_.dialogue;
    while (!remaining.empty()) {
        const auto end = remaining.find('\n');
        auto line = remaining.substr(0, end);
        if (line.ends_with('\r')) line.remove_suffix(1);
        view_.dialogueLines.emplace_back(line);
        if (end == std::string_view::npos) break;
        remaining.remove_prefix(end+1);
    }
}

void SceneView::scrollNpcDialogue(int amount) {
    view_.dialogueManualScroll = true;
    view_.dialogueOffset = std::clamp(view_.dialogueOffset + amount * lineHeight,
                                      0.f, endOfScroll(view_.dialogueLines.size())-lineHeight);
}

void SceneView::advanceNpcDialogue(float dt) {
    if (view_.dialogue.empty() || view_.dialogueManualScroll) return;
    // (elapsed milliseconds >> 2) * TBL rate, in 1/1024 pixel units.
    view_.dialogueOffset = std::min(endOfScroll(view_.dialogueLines.size()),
        view_.dialogueOffset + dt * (250.f/1024.f) * assets_.streams.speechScrollRate());
}

bool SceneView::closeNpcDialogue() {
    cancelNpcDialogue();
    return false;
}

bool SceneView::showNextNpcGossip() {
    if (!view_.npcMenu && view_.dialogue.empty())
        return false;
    if (npcView_.gossip.empty()) return false;
    const auto text = npcView_.gossip[view_.dialogueGossipTurn % npcView_.gossip.size()];
    ++view_.dialogueGossipTurn;
    view_.npcGossipTurns[view_.dialogueSpeaker] = view_.dialogueGossipTurn;
    openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, text);
    return true;
}

void SceneView::drawNpcDialogue() const {
    if (view_.audio.speech == NpcSpeechMode::AudioOnly) return;
    const auto box = speechBounds();
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette ? actPaletteBlends_[size_t(palette)].get() : paletteBlend_.get();
    if (blend) blend->drawRectangle(box, BLACK, true);
    originalMenu_.drawBorder({box.x-1, box.y-1, box.width+2, box.height+2});
    uiScissor(box.x, 12, box.width, 112);
    for (size_t index = 0; index < view_.dialogueLines.size(); ++index) {
        const float y = firstBaseline - int(view_.dialogueOffset) + float(index)*lineHeight;
        if (y < 12) continue;
        if (y > firstBaseline + lineHeight) break;
        assets_.questFont.drawBaseline(view_.dialogueLines[index], {box.x+16, y}, 1);
    }
    EndScissorMode();
}
bool SceneView::npcSpeechFinished() const {
    if (view_.audio.speech == NpcSpeechMode::AudioOnly)
        return assets_.streams.speechFinished().value_or(true);
    return !view_.dialogueManualScroll && view_.dialogueOffset >= endOfScroll(view_.dialogueLines.size());
}
} // namespace d2x

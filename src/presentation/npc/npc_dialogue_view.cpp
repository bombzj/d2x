#include "presentation/scene_view.hpp"
#include <algorithm>

namespace d2x {
namespace {
constexpr int visibleLines = 5;
constexpr int textSize = int(12 * classicPanelScale);
constexpr int lineHeight = int(20 * classicPanelScale);
constexpr float initialOffset = float(lineHeight);
Rectangle speechBounds() { return {(W - 400 * classicPanelScale) / 2, 0,
                                   400 * classicPanelScale, 120 * classicPanelScale}; }
int textWidth() { return int(speechBounds().width) - 32; }
float maxOffset(size_t lines) {
    return speechBounds().height - 24 + std::max(0, int(lines) - visibleLines) * lineHeight;
}

std::string fontText(std::string text) {
    // The original Latin DC6 font is byte-indexed. Keep MPQ text intact in content;
    // normalize only punctuation that this renderer cannot address as UTF-8.
    for (auto [from, to] : {std::pair{"\xe2\x80\x99", "'"}, {"\xe2\x80\x98", "'"},
                            {"\xe2\x80\x9c", "\""}, {"\xe2\x80\x9d", "\""},
                            {"\xe2\x80\xa6", "..."}, {"\xe2\x80\x94", "--"},
                            {"\xe2\x80\x93", "-"}}) {
        size_t at = 0;
        while ((at = text.find(from, at)) != std::string::npos) {
            text.replace(at, std::char_traits<char>::length(from), to);
            at += std::char_traits<char>::length(to);
        }
    }
    return text;
}
} // namespace

void SceneView::openNpcDialogue(EntityId object, std::string speaker, std::string text) {
    cancelNpcDialogue();
    displayNpcDialogue(object, std::move(speaker), std::move(text));
}

void SceneView::cancelNpcDialogue() {
    view_.pendingNpcDialogue.clear();
    view_.dialogue.clear();
    view_.dialogueLines.clear();
    view_.dialogueStatus.clear();
    view_.dialogueOffset = initialOffset;
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
    // Show the first line immediately, then continue the bottom-to-top crawl.
    view_.dialogueOffset = initialOffset;
    view_.dialogueManualScroll = false;
    view_.dialogueLines.clear();
    auto rendered = fontText(view_.dialogue);
    size_t start = 0;
    while (start <= rendered.size()) {
        size_t end = rendered.find('\n', start);
        if (end == std::string::npos)
            end = rendered.size();
        auto paragraph = rendered.substr(start, end - start);
        if (paragraph.empty())
            view_.dialogueLines.emplace_back();
        else {
            std::string line;
            size_t wordAt = 0;
            while (wordAt < paragraph.size()) {
                while (wordAt < paragraph.size() && paragraph[wordAt] == ' ')
                    ++wordAt;
                if (wordAt == paragraph.size())
                    break;
                size_t wordEnd = paragraph.find(' ', wordAt);
                if (wordEnd == std::string::npos)
                    wordEnd = paragraph.size();
                auto word = paragraph.substr(wordAt, wordEnd - wordAt);
                auto candidate = line.empty() ? word : line + ' ' + word;
                if (!line.empty() && speechPainter_.measure(candidate, textSize) > textWidth()) {
                    view_.dialogueLines.push_back(std::move(line));
                    line = std::move(word);
                } else
                    line = std::move(candidate);
                wordAt = wordEnd + 1;
            }
            if (!line.empty())
                view_.dialogueLines.push_back(std::move(line));
        }
        if (end == rendered.size())
            break;
        start = end + 1;
    }
}

void SceneView::scrollNpcDialogue(int amount) {
    view_.dialogueManualScroll = true;
    view_.dialogueOffset = std::clamp(view_.dialogueOffset + amount * lineHeight,
                                      initialOffset, maxOffset(view_.dialogueLines.size()));
}

void SceneView::advanceNpcDialogue(float dt) {
    if (view_.dialogue.empty() || view_.dialogueManualScroll) return;
    // Keep the existing text-only cadence until speech timing is available,
    // but scroll continuously from the bottom of the original-style window.
    view_.dialogueOffset = std::min(maxOffset(view_.dialogueLines.size()),
        view_.dialogueOffset + dt * lineHeight / 2.2f);
}

bool SceneView::closeNpcDialogue() {
    if (!view_.pendingNpcDialogue.empty()) {
        auto dialogue = std::move(view_.pendingNpcDialogue.front());
        view_.pendingNpcDialogue.pop_front();
        displayNpcDialogue(dialogue.object, std::move(dialogue.speaker), std::move(dialogue.text));
        return true;
    }
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
    auto bounds = speechBounds();
    DrawRectangleRec(bounds, {0, 0, 0, 200});
    DrawRectangleLinesEx(bounds, 1, gold);
    BeginScissorMode(int(bounds.x) + 14, int(bounds.y) + 8,
                     textWidth() + 4, int(bounds.height) - 16);
    const float firstY = bounds.y + bounds.height - 16 - view_.dialogueOffset;
    for (size_t index = 0; index < view_.dialogueLines.size(); ++index) {
        const float y = firstY + float(index) * lineHeight;
        if (y + lineHeight < bounds.y + 8) continue;
        if (y > bounds.y + bounds.height - 8) break;
        speechPainter_.label(view_.dialogueLines[index], int(bounds.x) + 16, int(y), textSize, WHITE);
    }
    EndScissorMode();
}
} // namespace d2x

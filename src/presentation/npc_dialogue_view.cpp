#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
namespace {
constexpr int visibleLines = 5;
constexpr int textSize = 18;
constexpr int lineHeight = 27;
Rectangle speechBounds() { return {(W - 540.f) / 2, 8, 540, 170}; }
int textWidth() { return int(speechBounds().width) - 32; }

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
    view_.dialogueObject = object;
    view_.shopOpen = false;
    view_.npcMenu = false;
    view_.shopPage = 0;
    view_.dialogueSpeaker = std::move(speaker);
    view_.dialogue = std::move(text);
    view_.dialogueStatus.clear();
    view_.dialogueScroll = 0;
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
    view_.dialogueScroll = std::clamp(view_.dialogueScroll + amount, 0,
                                      std::max(0, int(view_.dialogueLines.size()) - visibleLines));
}

void SceneView::closeNpcDialogue() {
    view_.dialogue.clear();
    view_.dialogueLines.clear();
    view_.npcMenu = true;
}

bool SceneView::showNextNpcGossip() {
    if (!view_.npcMenu && view_.dialogue.empty())
        return false;
    const auto *speech = gossipSpeech(session_.content().npcDialogues,
                                      view_.dialogueSpeaker, view_.dialogueGossipTurn);
    if (!speech)
        return false;
    ++view_.dialogueGossipTurn;
    openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, speech->text);
    return true;
}

void SceneView::drawNpcDialogue() const {
    auto bounds = speechBounds();
    DrawRectangleRec(bounds, {0, 0, 0, 200});
    DrawRectangleLinesEx(bounds, 1, gold);
    BeginScissorMode(int(bounds.x) + 14, int(bounds.y) + 8,
                     textWidth() + 4, int(bounds.height) - 16);
    for (int row = 0; row < visibleLines; ++row) {
        auto index = view_.dialogueScroll + row;
        if (index >= int(view_.dialogueLines.size()))
            break;
        speechPainter_.label(view_.dialogueLines[size_t(index)], int(bounds.x) + 16,
                             int(bounds.y) + 1 + row * lineHeight,
                             textSize, {230, 226, 212, 255});
    }
    EndScissorMode();
}
} // namespace d2x

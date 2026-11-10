#include "presentation/scene_view.hpp"
#include "core/random_seed.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
namespace {
// D2Client 7D200 / 7BB40, in native UI pixels.
constexpr int lineHeight = 18;
constexpr int speechTop = 12;
constexpr int scrollHeight = 112;
constexpr int firstBaseline = speechTop + scrollHeight;
Rectangle speechBounds() { return {float((W-325)/2), 7, 325, 122}; }
float endOfScroll(size_t lines) {
    return float(scrollHeight + 1 + std::max(1, int(lines)-1) * lineHeight);
}
void drawRetailSpeech(const ClassicFont &font, const std::vector<std::string> &lines, float x, int offset) {
    if (lines.empty()) return;
    constexpr int unit = 1024;
    constexpr int lineStep = lineHeight * unit;
    constexpr int fullLineLimit = scrollHeight - lineHeight;
    int first = -1, last = -1;
    for (int index = 0; index < int(lines.size()) && offset > 0; ++index) {
        const int relative = offset - index * lineStep;
        if (relative < 0) break;
        if (relative >= fullLineLimit * unit) continue;
        font.drawBaselineRows(lines[index], {x, float(firstBaseline - relative / unit)},
                              0, std::numeric_limits<int>::max());
        if (first < 0) first = index;
        last = index;
    }
    if (first > 0) {
        // 7BD50: the previous line is leaving through the upper edge.
        const int height = fullLineLimit - (offset - first * lineStep) / unit;
        font.drawBaselineRows(lines[first - 1], {x, float(speechTop + height)}, 0, std::min(lineHeight, height));
    }
    if (last >= 0 && last + 1 < int(lines.size())) {
        // 7BDB5: the next line enters from the lower edge, top rows first.
        const int relative = (offset - last * lineStep) / unit;
        const int skip = lineHeight - 1 - relative;
        font.drawBaselineRows(lines[last + 1], {x, float(firstBaseline + lineHeight - relative)},
                              skip, font.textHeight - skip);
    }
    if (last < 0) {
        const int relative = offset - (int(lines.size()) - 1) * lineStep;
        if (relative >= 0 && relative < scrollHeight * unit) {
            const int height = scrollHeight - 1 - relative / unit;
            font.drawBaselineRows(lines.back(), {x, float(speechTop + height)}, 0, std::min(lineHeight, height));
        }
    }
}
} // namespace

void SceneView::openNpcDialogue(EntityId object, std::string speaker, std::string text, std::optional<uint32_t> stringId) {
    cancelNpcDialogue();
    displayNpcDialogue(object, std::move(speaker), std::move(text));
    assets_.streams.speak(view_.dialogue, stringId, npcView_.retailSpeech);
}

void SceneView::cancelNpcDialogue() {
    assets_.streams.stopSpeech();
    npcScrollClock_.reset();
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
    npcSpeechOpened_ = NpcSpeechClock::now();
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
    if (npcView_.retailSpeech) {
        if (!npcScrollClock_ && !assets_.streams.speechReady()) return;
        const auto now = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            NpcSpeechClock::now().time_since_epoch()).count());
        if (!npcScrollClock_) {
            npcScrollClock_ = NpcScrollClock{now, now};
            return;
        }
        // 7BB95–7BBE8 estimates progress when the millisecond clock repeats.
        auto &clock = *npcScrollClock_;
        if (now == clock.last) {
            if (clock.average) clock.interpolated += clock.average;
            else ++clock.repeats;
        } else {
            clock.interpolated = 0;
            if (!clock.average && clock.repeats) clock.average = (now - clock.last) / clock.repeats;
            clock.last = now;
        }
        const auto ticks = ((now - clock.start) >> 2) + clock.interpolated;
        view_.dialogueOffset = std::min(endOfScroll(view_.dialogueLines.size()),
            float(ticks * assets_.streams.speechScrollRate()) / 1024.f);
        return;
    }
    view_.dialogueOffset = std::min(endOfScroll(view_.dialogueLines.size()),
        view_.dialogueOffset + dt * (250.f/1024.f) * assets_.streams.speechScrollRate());
}

bool SceneView::closeNpcDialogue() {
    cancelNpcDialogue();
    return false;
}

unsigned SceneView::chooseAkaraGossip() {
    if (!akaraGossipChoice_) {
        // 46910 draws from all 13 table rows, rejecting introductions.
        std::mt19937 random(freshSeed());
        std::uniform_int_distribution<unsigned> pick(0, 12);
        akaraGossipChoice_ = 0; // Original fallback is table row 2, TBL 13.
        for (int attempt = 0; attempt < 10; ++attempt) {
            const auto row = pick(random);
            if (row < 2 || !(npcView_.akaraGossipEligible & (1u << (row - 2)))) continue;
            akaraGossipChoice_ = row - 2;
            break;
        }
    }
    return *akaraGossipChoice_;
}

bool SceneView::showNextNpcGossip() {
    if (!view_.npcMenu && view_.dialogue.empty())
        return false;
    if (npcView_.gossip.empty()) return false;
    if (npcView_.akaraGossipEligible) {
        const auto row = chooseAkaraGossip();
        const auto text = npcView_.gossip[row];
        if (text.empty()) return false;
        openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, text, 13 + row);
        view_.dialogueFromMenu = true;
        return true;
    }
    const auto text = npcView_.gossip[view_.dialogueGossipTurn % npcView_.gossip.size()];
    ++view_.dialogueGossipTurn;
    view_.npcGossipTurns[view_.dialogueSpeaker] = view_.dialogueGossipTurn;
    openNpcDialogue(view_.dialogueObject, view_.dialogueSpeaker, text);
    view_.dialogueFromMenu = npcView_.retailSpeech;
    return true;
}

void SceneView::drawNpcDialogue() const {
    if (view_.audio.speech == NpcSpeechMode::AudioOnly &&
        !(npcView_.retailSpeech && npcView_.scrollMessage)) return;
    if (npcView_.retailSpeech && !npcScrollClock_) return;
    const auto box = speechBounds();
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette ? actPaletteBlends_[size_t(palette)].get() : paletteBlend_.get();
    if (blend) blend->drawRectangle(box, BLACK, true);
    originalMenu_.drawBorder({box.x-1, box.y-1, box.width+2, box.height+2});
    if (npcView_.retailSpeech) {
        drawRetailSpeech(assets_.questFont, view_.dialogueLines, box.x+16, int(view_.dialogueOffset * 1024));
        return;
    }
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
    if (!npcView_.retailSpeech && view_.audio.speech == NpcSpeechMode::AudioOnly)
        return assets_.streams.speechFinished().value_or(true);
    return !view_.dialogueManualScroll && view_.dialogueOffset >= endOfScroll(view_.dialogueLines.size());
}
bool SceneView::canSkipNpcSpeech() const {
    return NpcSpeechClock::now() - npcSpeechOpened_ >= std::chrono::milliseconds(100);
}
} // namespace d2x

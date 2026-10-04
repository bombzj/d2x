#include "presentation/scene_view.hpp"
#include "quest_panel.hpp"
#include <algorithm>
#include <array>

namespace d2x {
namespace {
constexpr float questAnimationSeconds = 3.f;
constexpr int questCompletedFrame = 24;
void drawArt(const Sprite *image, Rectangle bounds, Color tint = WHITE) {
    if (!image || !image->texture.id) return;
    auto &texture = image->texture;
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                   bounds, {0, 0}, 0, tint);
}
} // namespace

void SceneView::resetQuestAnimations() {
    questAnimations_ = {};
    for (size_t index = 0; index < questAnimations_.size(); ++index)
        questAnimations_[index].completed =
            questView_.entry(QuestId(index)).completed;
}

void SceneView::queueQuestAnimation(QuestId quest, bool completed) {
    const auto index = questIndex(quest);
    if (index >= questAnimations_.size()) return;
    auto &animation = questAnimations_[index];
    if (completed && !animation.completed) {
        animation.phase = QuestCompletionAnimation::Phase::Pending;
        animation.elapsed = 0;
    } else if (!completed) {
        animation.phase = QuestCompletionAnimation::Phase::Idle;
        animation.elapsed = 0;
    }
    animation.completed = completed;
}

void SceneView::advanceQuestAnimations(float dt) {
    for (size_t index = 0; index < questAnimations_.size(); ++index) {
        auto &animation = questAnimations_[index];
        if (!view_.questOpen || questView_.entry(QuestId(index)).act != view_.questAct) {
            if (animation.phase == QuestCompletionAnimation::Phase::Playing)
                animation.phase = QuestCompletionAnimation::Phase::Idle;
            continue;
        }
        if (animation.phase == QuestCompletionAnimation::Phase::Pending) {
            animation.phase = QuestCompletionAnimation::Phase::Playing;
            animation.elapsed = 0;
            view_.questSelected = int(questView_.entry(QuestId(index)).displaySlot);
            assets_.audio.play("quest_done");
        } else if (animation.phase == QuestCompletionAnimation::Phase::Playing) {
            animation.elapsed += dt;
            const auto &art = assets_.questIcons[questView_.entry(QuestId(index)).icon];
            if (animation.elapsed * art.count / questAnimationSeconds >= questCompletedFrame)
                animation.phase = QuestCompletionAnimation::Phase::Idle;
        }
    }
}

void SceneView::drawQuests(Vec) const {
    if (!view_.questOpen) return;
    const auto panel = questBounds();
    drawPanelFrame(false);
    const float tileWidth = float(assets_.questBackground.frames.at(0).texture.width);
    const float tileHeight = float(assets_.questBackground.frames.at(0).texture.height);
    for (int index = 0; index < 4; ++index) {
        const auto &tile = assets_.questBackground.frames.at(index).texture;
        DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
            {panel.x + (index % 2) * tileWidth * inventoryScale,
             panel.y + (index / 2) * tileHeight * inventoryScale,
             tile.width * inventoryScale, tile.height * inventoryScale}, {0, 0}, 0, WHITE);
    }
    const int tabCount = questView_.tabCount;
    for (int act = 0; act < tabCount; ++act)
        drawArt(assets_.questTabs.frame(0, act * 2 + (act == view_.questAct ? 0 : 1)), questTabBounds(act));
    for (int index = 0; index < int(questView_.quests(view_.questAct).size()); ++index) {
        const auto bounds = questIconBounds(index);
        const auto id = questView_.displayed(view_.questAct, size_t(index));
        const auto stateIndex = questIndex(id);
        const auto &record = questView_.entry(id);
        int artFrame = !record.active ? 26 : record.completed ? 24 : 25;
        drawArt(assets_.questSockets.frame(0, 0), bounds);
        const auto iconBounds = questArtRect(24.f + float(index % 3) * 100.f,
                                            37.f + float(index / 3) * 95.f, 72, 86);
        const auto artIndex = record.icon;
        const auto &art = assets_.questIcons[artIndex];
        const auto &animation = questAnimations_[stateIndex];
        const bool completing = animation.phase == QuestCompletionAnimation::Phase::Playing;
        if (completing)
            artFrame = std::min(questCompletedFrame,
                               int(animation.elapsed * art.count / questAnimationSeconds));
        const auto *icon = art.frame(0, artFrame);
        drawArt(icon, iconBounds);
        const auto face = assets_.questFaces[artIndex];
        if (icon && !completing && view_.questPressed == index && record.active && face.width > 2 && face.height > 2) {
            const auto *inactive = art.frame(0, 26);
            const float scaleX = iconBounds.width / icon->texture.width;
            const float scaleY = iconBounds.height / icon->texture.height;
            const Rectangle destination{iconBounds.x + face.x * scaleX, iconBounds.y + face.y * scaleY,
                                        face.width * scaleX, face.height * scaleY};
            DrawTexturePro(inactive->texture, face, destination, {0, 0}, 0, WHITE);
            DrawTexturePro(icon->texture, {face.x + 2, face.y, face.width - 2, face.height - 2},
                           {destination.x, destination.y + 2 * scaleY,
                            destination.width - 2 * scaleX, destination.height - 2 * scaleY},
                           {0, 0}, 0, WHITE);
        }
        if (index == view_.questSelected)
            drawArt(assets_.questSockets.frame(0, 1), bounds);
    }
    if (view_.questSelected >= 0 && view_.questSelected < int(questView_.quests(view_.questAct).size())) {
        const int index = view_.questSelected;
        const auto id = questView_.displayed(view_.questAct, size_t(index));
        const auto &record = questView_.entry(id);
        const auto &heading = record.title;
        const auto titleBounds = questArtRect(10, 233, 300, 21);
        const int textSize = int(12 * inventoryScale);
        speechPainter_.inBox(heading, titleBounds, textSize, WHITE);
        if (record.description) {
            const auto &value = *record.description;
            std::string line;
            int y = int(questArtRect(10, 253, 300, 0).y);
            const int left = int(questArtRect(10, 0, 0, 0).x);
            const int width = int(questArtRect(0, 0, 300, 0).width);
            const int lastLine = int(questArtRect(0, 373, 0, 0).y);
            auto flush = [&] {
                if (!line.empty()) {
                    if (y <= lastLine)
                        speechPainter_.label(line, left, y, textSize, WHITE);
                    y += int(20 * inventoryScale);
                    line.clear();
                }
            };
            size_t at = 0;
            while (at < value.size() && y <= lastLine) {
                if (value[at] == '\n') { flush(); y += 7; ++at; continue; }
                while (at < value.size() && value[at] == ' ') ++at;
                size_t end = value.find_first_of(" \n", at);
                if (end == std::string::npos) end = value.size();
                if (end == at) { ++at; continue; }
                auto word = value.substr(at, end - at);
                auto candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && speechPainter_.measure(candidate, textSize) > width) flush();
                line += line.empty() ? word : " " + word;
                at = end;
            }
            flush();
            if (record.tombSymbol) {
                const auto &symbol = assets_.tombSymbols.at(size_t(*record.tombSymbol));
                const auto *symbolFrame = symbol.frame(0, int(view_.animationTime * 25));
                const float top = std::max(290.f, (float(y) - questArtRect(0, 0, 0, 0).y) / inventoryScale + 4);
                const float size = std::min(70.f, 381.f - top);
                if (size > 0) {
                    BeginBlendMode(BLEND_ADDITIVE);
                    drawArt(symbolFrame, questArtRect((320 - size) / 2, top, size, size));
                    EndBlendMode();
                }
            }
        }
    }
    const auto close = questCloseBounds();
    drawArt(assets_.questClose.frame(0, 10), close);
    // Playback awaits the original speech resources and the replay selection rule.
    drawArt(assets_.questReplay.frame(0, 0), questReplayBounds(), {128, 128, 128, 195});
}
} // namespace d2x

#include "gameplay/session/session.hpp"
#include "presentation/scene_view.hpp"
#include "quest_panel.hpp"
#include <algorithm>
#include <array>

namespace d2x {
namespace {
// The on-screen order differs from the a1qN resource numbering.
constexpr std::array questArtNumbers = {1, 2, 4, 5, 3, 6};
constexpr std::array titleKeys = {
    "qstsa1q1", "qstsa1q2", "qstsa1q4", "qstsa1q5", "qstsa1q3", "qstsa1q6"};
constexpr std::array completedStages = {
    uint32_t(DenStage::Rewarded), uint32_t(BurialStage::Rewarded),
    uint32_t(CainStage::Rewarded), uint32_t(TowerStage::CountessSlain),
    uint32_t(ToolsStage::Imbued), uint32_t(SlaughterStage::Completed)};
constexpr float questAnimationSeconds = 3.f;
constexpr int questCompletedFrame = 24;
// Internal transition stages are not the original quest-log string IDs.
// Keys are from the mounted TBL; A1Q3's reward-pending status uses qstsa1q32b.
std::string_view descriptionKey(ActOneQuest quest, uint32_t stage) {
    switch (quest) {
    case ActOneQuest::DenOfEvil:
        if (stage == uint32_t(DenStage::Assigned)) return "qstsa1q11";
        if (stage == uint32_t(DenStage::Entered)) return "qstsa1q12";
        if (stage == uint32_t(DenStage::Cleared)) return "qstsa1q15";
        break;
    case ActOneQuest::SistersBurialGrounds:
        if (stage == uint32_t(BurialStage::Assigned)) return "qstsa1q21";
        if (stage == uint32_t(BurialStage::Entered)) return "qstsa1q22";
        if (stage == uint32_t(BurialStage::BloodRavenSlain)) return "qstsa1q23";
        break;
    case ActOneQuest::SearchForCain:
        if (stage == uint32_t(CainStage::Assigned)) return "qstsa1q41";
        if (stage == uint32_t(CainStage::TreeOpened)) return "qstsa1q41";
        if (stage == uint32_t(CainStage::BarkAcquired)) return "qstsa1q42";
        if (stage == uint32_t(CainStage::ScrollTranslated)) return "qstsa1q43";
        if (stage == uint32_t(CainStage::PortalOpened)) return "qstsa1q44";
        if (stage == uint32_t(CainStage::TristramEntered)) return "qstsa1q44";
        if (stage == uint32_t(CainStage::Rescued)) return "qstsa1q46";
        break;
    case ActOneQuest::ForgottenTower:
        if (stage == uint32_t(TowerStage::TomeRead)) return "qstsa1q51";
        if (stage == uint32_t(TowerStage::TowerEntered)) return "qstsa1q51a";
        if (stage == uint32_t(TowerStage::CellarEntered)) return "qstsa1q52";
        break;
    case ActOneQuest::ToolsOfTheTrade:
        if (stage == uint32_t(ToolsStage::Assigned) ||
            stage == uint32_t(ToolsStage::BarracksEntered) ||
            stage == uint32_t(ToolsStage::MalusDropped)) return "qstsa1q31";
        if (stage == uint32_t(ToolsStage::MalusAcquired)) return "qstsa1q32";
        if (stage == uint32_t(ToolsStage::RewardReady)) return "qstsa1q32b";
        break;
    case ActOneQuest::SistersToTheSlaughter:
        if (stage == uint32_t(SlaughterStage::Assigned)) return "qstsa1q61";
        if (stage == uint32_t(SlaughterStage::CatacombsEntered)) return "qstsa1q62";
        if (stage == uint32_t(SlaughterStage::AndarielSlain) ||
            stage == uint32_t(SlaughterStage::PassageReady)) return "qstsa1q63";
        break;
    default: break;
    }
    return {};
}
void drawArt(const Sprite *image, Rectangle bounds, Color tint = WHITE) {
    if (!image || !image->texture.id) return;
    auto &texture = image->texture;
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                   bounds, {0, 0}, 0, tint);
}
} // namespace

void SceneView::resetQuestAnimations() {
    questAnimations_ = {};
    for (size_t index = 0; index < questDisplayOrder.size(); ++index)
        questAnimations_[index].completed =
            session_.quest(questDisplayOrder[index]).stage >= completedStages[index];
}

void SceneView::queueQuestAnimation(ActOneQuest quest, uint32_t stage) {
    const auto found = std::find(questDisplayOrder.begin(), questDisplayOrder.end(), quest);
    if (found == questDisplayOrder.end()) return;
    const auto index = size_t(found - questDisplayOrder.begin());
    auto &animation = questAnimations_[index];
    const bool completed = stage >= completedStages[index];
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
        if (!view_.questOpen) {
            if (animation.phase == QuestCompletionAnimation::Phase::Playing)
                animation.phase = QuestCompletionAnimation::Phase::Idle;
            continue;
        }
        if (animation.phase == QuestCompletionAnimation::Phase::Pending) {
            animation.phase = QuestCompletionAnimation::Phase::Playing;
            animation.elapsed = 0;
            view_.questSelected = int(index);
            assets_.audio.play("quest_done");
        } else if (animation.phase == QuestCompletionAnimation::Phase::Playing) {
            animation.elapsed += dt;
            const auto &art = assets_.actOneQuestIcons[size_t(questArtNumbers[index] - 1)];
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
    // Only reached acts have tabs. This session currently implements Act I.
    const int tabCount = 1;
    for (int act = 0; act < tabCount; ++act)
        drawArt(assets_.questTabs.frame(0, act * 2 + (act ? 1 : 0)), questTabBounds(act));
    auto title = [&](int index) -> std::string {
        auto found = session_.content().actOneQuestStrings.find(titleKeys[size_t(index)]);
        return found == session_.content().actOneQuestStrings.end()
                   ? titleKeys[size_t(index)] : found->second;
    };
    for (int index = 0; index < int(questDisplayOrder.size()); ++index) {
        const auto bounds = questIconBounds(index);
        const auto &record = session_.quest(questDisplayOrder[size_t(index)]);
        int artFrame = !record.stage ? 26 :
                             record.stage >= completedStages[size_t(index)] ? 24 : 25;
        drawArt(assets_.questSockets.frame(0, 0), bounds);
        const auto iconBounds = questArtRect(24.f + float(index % 3) * 100.f,
                                            37.f + float(index / 3) * 95.f, 72, 86);
        const auto artIndex = size_t(questArtNumbers[size_t(index)] - 1);
        const auto &art = assets_.actOneQuestIcons[artIndex];
        const auto &animation = questAnimations_[size_t(index)];
        const bool completing = animation.phase == QuestCompletionAnimation::Phase::Playing;
        if (completing)
            artFrame = std::min(questCompletedFrame,
                               int(animation.elapsed * art.count / questAnimationSeconds));
        const auto *icon = art.frame(0, artFrame);
        drawArt(icon, iconBounds);
        const auto face = assets_.actOneQuestFaces[artIndex];
        if (icon && !completing && view_.questPressed == index && record.stage && face.width > 2 && face.height > 2) {
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
    if (view_.questSelected >= 0 && view_.questSelected < int(questDisplayOrder.size())) {
        const int index = view_.questSelected;
        const auto &record = session_.quest(questDisplayOrder[size_t(index)]);
        const auto heading = title(index);
        const auto titleBounds = questArtRect(10, 233, 300, 21);
        const int textSize = int(12 * inventoryScale);
        speechPainter_.inBox(heading, titleBounds, textSize, WHITE);
        std::string_view key;
        if (record.stage > 0) {
            if (completedStages[size_t(index)] && record.stage >= completedStages[size_t(index)])
                key = "qstsComplete";
            else key = descriptionKey(questDisplayOrder[size_t(index)], record.stage);
        }
        const unsigned remaining = index == 0 && record.stage == uint32_t(DenStage::Entered)
                                       ? session_.denMonstersRemaining().value_or(0) : 0;
        if (remaining > 0 && remaining <= 5)
            key = remaining == 1 ? "qstsa1q140" : "qstsa1q14";
        auto found = session_.content().actOneQuestStrings.find(key);
        if (found != session_.content().actOneQuestStrings.end()) {
            auto value = found->second;
            if (key == "qstsa1q14") value += std::to_string(remaining);
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
        }
    }
    const auto close = questCloseBounds();
    drawArt(assets_.questClose.frame(0, 10), close);
    // Playback awaits the original speech resources and the replay selection rule.
    drawArt(assets_.questReplay.frame(0, 0), questReplayBounds(), {128, 128, 128, 195});
}
} // namespace d2x

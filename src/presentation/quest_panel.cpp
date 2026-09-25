#include "scene_view.hpp"
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
        const int artFrame = !record.stage ? 26 :
                             record.stage >= completedStages[size_t(index)] ? 24 : 25;
        drawArt(assets_.questSockets.frame(0, 0), bounds);
        const auto iconBounds = questArtRect(24.f + float(index % 3) * 100.f,
                                            37.f + float(index / 3) * 95.f, 72, 86);
        drawArt(assets_.actOneQuestIcons[size_t(questArtNumbers[size_t(index)] - 1)]
                    .frame(0, artFrame), iconBounds);
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

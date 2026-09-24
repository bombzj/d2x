#include "scene_view.hpp"
#include "quest_panel.hpp"
#include <algorithm>
#include <array>

namespace d2x {
namespace {
constexpr std::array quests = {
    ActOneQuest::DenOfEvil, ActOneQuest::SistersBurialGrounds,
    ActOneQuest::SearchForCain, ActOneQuest::ForgottenTower,
    ActOneQuest::ToolsOfTheTrade, ActOneQuest::SistersToTheSlaughter};
constexpr std::array titleKeys = {
    "qstsa1q1", "qstsa1q2", "qstsa1q4", "qstsa1q5", "qstsa1q3", "qstsa1q6"};
constexpr std::array completedStages = {4u, 4u, 8u, 4u, 6u, 5u};
void drawArt(const Sprite *image, Rectangle bounds, Color tint = WHITE) {
    if (!image || !image->texture.id) return;
    auto &texture = image->texture;
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                   bounds, {0, 0}, 0, tint);
}
} // namespace

void SceneView::drawQuests(Vec mouse) const {
    if (!view_.questOpen) return;
    const auto panel = questBounds();
    DrawRectangle(int(panel.x) - 8, 0, int(panel.width) + 12, H - HUD, {0, 0, 0, 150});
    const float tileWidth = float(assets_.questBackground.frames.at(0).texture.width);
    const float tileHeight = float(assets_.questBackground.frames.at(0).texture.height);
    for (int index = 0; index < 4; ++index) {
        const auto &tile = assets_.questBackground.frames.at(index).texture;
        DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
            {panel.x + (index % 2) * tileWidth * inventoryScale,
             panel.y + (index / 2) * tileHeight * inventoryScale,
             tile.width * inventoryScale, tile.height * inventoryScale}, {0, 0}, 0, WHITE);
    }
    drawArt(assets_.questTabs.frame(0, 0), questArtRect(26, 12, 76, 27));
    auto title = [&](int index) -> std::string {
        auto found = session_.content().actOneQuestStrings.find(titleKeys[size_t(index)]);
        return found == session_.content().actOneQuestStrings.end()
                   ? titleKeys[size_t(index)] : found->second;
    };
    if (view_.questSelected < 0) {
        for (int index = 0; index < int(quests.size()); ++index) {
            auto bounds = questIconBounds(index);
            auto &record = session_.quest(quests[size_t(index)]);
            drawArt(assets_.questSockets.frame(0, 0), bounds);
            drawArt(assets_.questDone.frame(0, index), bounds,
                    record.stage ? WHITE : Color{105, 105, 105, 255});
            if (CheckCollisionPointRec(rv(mouse), bounds)) {
                drawArt(assets_.questSockets.frame(0, 1), bounds);
                auto text = title(index);
                painter_.label(text, int(panel.x + 12), int(panel.y + 41), 14, gold);
            }
        }
    } else if (view_.questSelected < int(quests.size())) {
        const int index = view_.questSelected;
        auto &record = session_.quest(quests[size_t(index)]);
        drawArt(assets_.questDone.frame(0, index), questArtRect(122, 45, 72, 85));
        auto heading = title(index);
        painter_.label(heading, int(questArtRect(20, 145, 280, 30).x),
                       int(questArtRect(20, 145, 280, 30).y), 19, gold);
        std::string key = "noactivequest";
        if (record.stage > 0) {
            if (completedStages[size_t(index)] && record.stage >= completedStages[size_t(index)])
                key = "qstsComplete";
            else key = std::string(titleKeys[size_t(index)]) + std::to_string(record.stage);
        }
        auto found = session_.content().actOneQuestStrings.find(key);
        if (found != session_.content().actOneQuestStrings.end()) {
            const auto &value = found->second;
            std::string line;
            int y = int(questArtRect(22, 180, 275, 0).y);
            const int left = int(questArtRect(22, 0, 0, 0).x);
            const int width = int(questArtRect(0, 0, 275, 0).width);
            auto flush = [&] {
                if (!line.empty()) {
                    painter_.label(line, left, y, 13, parchment);
                    y += 19;
                    line.clear();
                }
            };
            size_t at = 0;
            while (at < value.size() && y < questArtRect(0, 370, 0, 0).y) {
                if (value[at] == '\n') { flush(); y += 7; ++at; continue; }
                while (at < value.size() && value[at] == ' ') ++at;
                size_t end = value.find_first_of(" \n", at);
                if (end == std::string::npos) end = value.size();
                if (end == at) { ++at; continue; }
                auto word = value.substr(at, end - at);
                auto candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && painter_.measure(candidate, 13) > width) flush();
                line += line.empty() ? word : " " + word;
                at = end;
            }
            flush();
        }
        painter_.label("Back", int(questBackBounds().x + 8),
                       int(questBackBounds().y + 8), 14, gold);
    }
    const auto close = questCloseBounds();
    const auto color = CheckCollisionPointRec(rv(mouse), close) ? gold : parchment;
    DrawLineEx({close.x + 9, close.y + 9},
               {close.x + close.width - 9, close.y + close.height - 9}, 2, color);
    DrawLineEx({close.x + close.width - 9, close.y + 9},
               {close.x + 9, close.y + close.height - 9}, 2, color);
}
} // namespace d2x

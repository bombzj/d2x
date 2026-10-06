#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/model/state.hpp"
#include "world/region.hpp"
#include "gameplay/items/inventory.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "presentation/scene_view.hpp"
#include "quest_panel.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include <algorithm>
#include <cmath>
namespace d2x {
int SceneView::gameMenuItemCount() const {
    return view_.gameMenuPage == 2 ? 6 : view_.gameMenuPage == 1 ? 5 : 3;
}
Rectangle SceneView::gameMenuItemBounds(int index) const {
    if (view_.gameMenuPage == 2)
        return {130.f, 130.f + index * 65.f, W - 260.f, 46.f};
    if (view_.gameMenuPage == 1)
        return {180.f, 160.f + index * 70.f, W - 360.f, 48.f};
    float width = 0, height = 0, totalHeight = 0, top = 0;
    for (size_t row = 0; row < assets_.gameMenuLabels.size(); ++row) {
        float rowWidth = 0, rowHeight = 0;
        for (const auto &part : assets_.gameMenuLabels[row].frames) {
            rowWidth += part.texture.width;
            rowHeight = std::max(rowHeight, float(part.texture.height));
        }
        if (int(row) < index) top += rowHeight + 10;
        if (int(row) == index) { width = rowWidth; height = rowHeight; }
        totalHeight += rowHeight + (row + 1 < assets_.gameMenuLabels.size() ? 10 : 0);
    }
    return {(W - width * hudScale) * .5f,
            (H - totalHeight * hudScale) * .5f + top * hudScale,
            width * hudScale, height * hudScale};
}
int SceneView::gameMenuAt(Vec mouse) const {
    for (int index = 0; index < gameMenuItemCount(); ++index)
        if (CheckCollisionPointRec(rv(mouse), gameMenuItemBounds(index))) return index;
    return -1;
}
void SceneView::drawGameMenu() const {
    if (!view_.gameMenuOpen) return;
    DrawRectangle(0, 0, W, H, {0, 0, 0, 100});
    if (view_.gameMenuPage) {
        auto size = [](const GpuAnimation &art) {
            Vec result;
            for (const auto &part : art.frames) {
                result.x += part.texture.width;
                result.y = std::max(result.y, float(part.texture.height));
            }
            return result;
        };
        auto label = [&](const GpuAnimation &art, Vec position, float scale, Color tint = WHITE) {
            for (const auto &part : art.frames) {
                DrawTexturePro(part.texture, {0, 0, float(part.texture.width), float(part.texture.height)},
                    {position.x, position.y, part.texture.width * scale, part.texture.height * scale},
                    {0, 0}, 0, tint);
                position.x += part.texture.width * scale;
            }
        };
        if (view_.gameMenuPage == 2) {
            const auto dimensions = size(assets_.automapOptionsTitle);
            const float scale = std::min(hudScale, (W - 160.f) / dimensions.x);
            label(assets_.automapOptionsTitle, {(W - dimensions.x * scale) * .5f, 42}, scale);
        }
        for (int index = 0; index < gameMenuItemCount(); ++index) {
            const auto bounds = gameMenuItemBounds(index);
            const auto &art = view_.gameMenuPage == 1 ? assets_.optionsMenuLabels[size_t(index)]
                : index == 5 ? assets_.optionsMenuLabels[4] : assets_.automapOptionLabels[size_t(index)];
            const auto dimensions = size(art);
            const float available = view_.gameMenuPage == 2 && index < 5 ? bounds.width * .68f : bounds.width;
            const float scale = std::min(hudScale, std::min(available / dimensions.x, bounds.height / dimensions.y));
            const bool centered = view_.gameMenuPage == 1 || index == 5;
            label(art, {centered ? (W - dimensions.x * scale) * .5f : bounds.x,
                    bounds.y + (bounds.height - dimensions.y * scale) * .5f}, scale);
            if (view_.gameMenuPage == 2 && index < 5) {
                const int value = index == 0 ? (view_.automapLarge ? 0 : 1)
                    : index == 1 ? (view_.automapFade == AutomapFade::No ? 7
                        : view_.automapFade == AutomapFade::Everything ? 8
                        : view_.automapFade == AutomapFade::Center ? 9 : 6)
                    : index == 2 ? (view_.automapCenterWhenCleared ? 5 : 4)
                    : index == 3 ? (view_.automapParty ? 5 : 4) : (view_.automapNames ? 5 : 4);
                const auto &valueArt = assets_.automapOptionValues[size_t(value)];
                const auto valueSize = size(valueArt);
                const float valueScale = std::min(hudScale, std::min(bounds.width * .28f / valueSize.x,
                                                                    bounds.height / valueSize.y));
                label(valueArt, {bounds.x + bounds.width - valueSize.x * valueScale,
                    bounds.y + (bounds.height - valueSize.y * valueScale) * .5f}, valueScale);
            }
        }
        const auto selected = gameMenuItemBounds(view_.gameMenuSelected);
        const auto &marker = assets_.gameMenuMarker;
        const int frame = int(view_.gameMenuTime * marker.count) % marker.count;
        for (bool right : {false, true}) {
            const auto *image = marker.frame(0, right ? frame : marker.count - 1 - frame);
            const float side = 46.f;
            DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
                {right ? W - 82.f : 36.f, selected.y + (selected.height - side) * .5f, side, side},
                {0, 0}, 0, WHITE);
        }
        if (view_.noticeError && view_.noticeTime > 0)
            painter_.centered(view_.lootNotice, H - HUD - 30, 14, {245, 166, 135, 255});
        return;
    }
    float widest = 0;
    for (int index = 0; index < int(assets_.gameMenuLabels.size()); ++index) {
        auto bounds = gameMenuItemBounds(index);
        widest = std::max(widest, bounds.width);
        for (const auto &part : assets_.gameMenuLabels[size_t(index)].frames) {
            const auto &texture = part.texture;
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           {bounds.x, bounds.y, texture.width * hudScale, texture.height * hudScale},
                           {0, 0}, 0, WHITE);
            bounds.x += texture.width * hudScale;
        }
    }
    const auto selected = gameMenuItemBounds(view_.gameMenuSelected);
    const auto &marker = assets_.gameMenuMarker;
    const int frame = int(view_.gameMenuTime * marker.count) % marker.count;
    for (bool right : {false, true}) {
        const auto *image = marker.frame(0, right ? frame : marker.count - 1 - frame);
        const float side = 54 * hudScale;
        const float left = right ? (W + widest) * .5f + 26 * hudScale
                                 : (W - widest) * .5f - 80 * hudScale;
        DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
                       {left, selected.y + (selected.height - side) * .5f, side, side},
                       {0, 0}, 0, WHITE);
    }
    if (view_.noticeError && view_.noticeTime > 0) {
        std::string line;
        int top = H - HUD - 90;
        for (char letter : view_.lootNotice) {
            if (letter == '\n' || painter_.measure(line + letter, 14) > W - 80) {
                painter_.centered(line, top, 14, {245, 166, 135, 255});
                line.clear();
                top += 17;
                if (top > H - 20) break;
            }
            if (letter != '\n') line += letter;
        }
        if (!line.empty()) painter_.centered(line, top, 14, {245, 166, 135, 255});
    }
}
void SceneView::drawHud() const {
    const auto &sim = localSession().state();
    if (view_.questNotice && !view_.questOpen && !view_.capturesWorldInput() &&
        !view_.characterOpen && !view_.inventory.storage && !view_.inventory.cubeOpen) {
        const auto bounds = questNoticeBounds();
        if (const auto *button = assets_.attributeButtons.frame(0, 0))
            DrawTexturePro(button->texture, {0, 0, float(button->texture.width), float(button->texture.height)},
                           bounds, {0, 0}, 0, WHITE);
        const auto label = localSession().content().questStrings.find("newquestlog");
        if (label != localSession().content().questStrings.end())
            painter_.label(label->second, int(bounds.x + (bounds.width - painter_.measure(label->second, 13)) / 2),
                           int(bounds.y) - 19, 13, WHITE);
    }
    if (view_.debug && !view_.npcMenu && view_.dialogue.empty() && !view_.shopOpen) {
        const int legendX = hirelingPortraitVisible() ? int(74 * classicPanelScale) : 22;
        painter_.label("D2X", legendX, 20, 20, gold);
        painter_.label("CLASSIC ENGINE / C++", legendX + 50, 24, 10, {154, 149, 129, 255});
        painter_.label("LV " + std::to_string(sim.player.character.level) + "  XP " +
                           std::to_string(sim.player.character.experience),
                       legendX, 48, 10, {154, 149, 129, 255});
        int worldWidth = view_.inventory.open && !view_.inventory.storage ? int(inventoryBounds().x) : W;
        const auto &regionName = localSession().region().definition.name;
        painter_.label(regionName, (worldWidth - painter_.measure(regionName, 20)) / 2, 20, 20, gold);
        const std::string subtitle = "ACT I  /  ORIGINAL MPQ ASSETS";
        painter_.label(subtitle, (worldWidth - painter_.measure(subtitle, 10)) / 2, 46, 10,
                       {137, 136, 112, 255});
        painter_.label("TAB Map", W - 204, 191, 10, {153, 144, 118, 255});
        painter_.label("Ctrl+F1 Help", W - 204, 207, 10, {153, 144, 118, 255});
    }
    if (!sim.message.empty())
        painter_.centered(sim.message, H - HUD - 35, 16, {218, 176, 95, 255});
    if (view_.noticeTime > 0) {
        int width = painter_.measure(view_.lootNotice, 14) + 32;
        float centerX =
            view_.inventory.open && !view_.inventory.storage ? inventoryBounds().x * .5f : W * .5f;
        frame({centerX - width * .5f, H - HUD - 139.f, float(width), 34},
              view_.noticeError ? Color{190, 91, 67, 255} : gold);
        painter_.label(view_.lootNotice, int(centerX - (width - 32) * .5f), H - HUD - 129, 14,
                       view_.noticeError ? Color{245, 166, 135, 255} : parchment);
    }
    int statusY = H - HUD - 28;
    for (const auto &status : localSession().shrineStatuses()) {
        const int seconds = std::max(0, int(std::ceil(status.until - sim.time)));
        const std::string label = status.name + "  " + std::to_string(seconds) + "s";
        DrawRectangle(8, statusY - 3, painter_.measure(label, 12) + 16, 21, {0, 0, 0, 205});
        painter_.label(label, 16, statusY, 12, gold);
        statusY -= 23;
    }
}
void SceneView::drawHelp() const {

    DrawRectangle(0, 0, W, H, {0, 0, 0, 155});
    frame({W / 2.f - 260, 90, 520, 510});
    painter_.centered("FIELD MANUAL", 140, 25, gold);
    painter_.centered("Diablo II classic resource simulation", 178, 12);
    const char *lines[] = {"Left click / hold          Move / Attack / Talk / Pick up",
                           "Hold Alt                   Show ground item names",
                           "I / A / S                  Inventory / Character / Skills",
                           "Arrow keys                 Move in screen directions",
                           "Right click                Cast; click slot to choose",
                           "1 through 4 / B            Drink belt potion / Expand belt",
                           "Hover skill, F1-F8         Bind selected mouse skill",
                           "F1 through F8              Select bound mouse skill",
                           "R                          Toggle walk / run",
                           "Tab / V                    Automap / Switch map side",
                           "Ctrl+F3 / Ctrl+F4          Collision / Walk to stash",
                           "F11 / Ctrl + F11           Save / Load",
                           "F12 / Ctrl+F12             Map names / Screenshot",
                           "Ctrl+F1                   Close this panel"};
    for (int i = 0; i < int(std::size(lines)); i++)
        painter_.label(lines[i], W / 2 - 194, 216 + i * 23, 12,
                       i < 4 ? parchment : Color{150, 148, 132, 255});
    painter_.centered("Ctrl+Alt+G/E: gold/XP   A/T: reset points   W: waypoints", 575, 11, gold);
}
void SceneView::drawEnemyBar(std::string_view title, std::optional<float> life,
                              std::string_view description, Color color) const {
    const std::string name(title), details(description);
    // Diablerie EnemyBar.prefab: shared top-center original-font name and life fill.
    int fontSize = 16;
    while (fontSize > 1 && painter_.measure(name, fontSize) > W - 40) --fontSize;
    const int width = std::clamp(painter_.measure(name, fontSize) + 8, 150, W - 32);
    const int left = (W - width) / 2;
    DrawRectangle(left, 22, width, 20, {0, 0, 0, 122});
    if (life) DrawRectangle(left, 22, int(width * std::clamp(*life, 0.f, 1.f)),
                            20, {191, 6, 6, 64});
    painter_.centered(name, 24, fontSize, color);
    int descriptionSize = 12;
    while (descriptionSize > 1 && painter_.measure(details, descriptionSize) > W - 40) --descriptionSize;
    if (!details.empty()) painter_.centered(details, 45, descriptionSize, color);
}
void SceneView::drawDeathNotice() const {
    if (!characterView_.dead) return;
    painter_.centered("YOU HAVE DIED", 250, 32, {187, 46, 30, 255});
    painter_.centered("PRESS ESC TO CONTINUE", 300, 32, {187, 46, 30, 255});
}
void SceneView::drawUi(Vec mouse) const {
    drawControlPanel();
    drawHirelingPortrait();
    if (multiplayer() && view_.noticeTime > 0)
        painter_.centered(view_.lootNotice, H - HUD - 35, 14, view_.noticeError ? RED : parchment);
    drawStorage(mouse);
    drawCube(mouse);
    drawOrifice(mouse);
    drawCharacter(mouse);
    drawHireling(mouse);
    drawQuests(mouse);
    drawSkillTree(mouse);
    if (view_.shopOpen) drawNpcShop(mouse);
    drawInventory(mouse);
    drawBelt(mouse);
    drawDeathNotice();
    if (view_.help)
        drawHelp();
    if (view_.travelMenu && view_.waypointSource)
        drawWaypointMenu(mouse);
    drawSkillControls(mouse);
    if (view_.npcMenu) drawNpcMenu(mouse);
    drawHirelingList(mouse);
    if (!view_.dialogue.empty()) drawNpcDialogue();
    const bool itemCursor = drawInventoryCursor(mouse);
    drawGameMenu();
    if (!itemCursor) {
        const Sprite *pointer = assets_.cursor.frame(0, 0);
        Vec hotspot = handCursorHotspot(pointer);
        if (const auto &targeting = view_.inventory.identify) {
            const auto *item = inventoryView_.item(targeting->id);
            const auto *definition = item ? inventoryView_.definition(item->definition) : nullptr;
            if (definition && item->revision == targeting->revision && definition->targetCursor >= 0) {
                pointer = assets_.targetingCursors.frame(0, definition->targetCursor);
                // spells.dc6 points from its top-left; it does not use ohand's origin.
                hotspot = {};
            }
        }
        cursorSprite(pointer, mouse, hotspot);
    }
}
} // namespace d2x

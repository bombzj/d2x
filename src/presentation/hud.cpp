#include "scene_view.hpp"
#include <algorithm>
#include <cmath>
namespace d2x {
void SceneView::drawHud() const {
    const auto &sim = session_.state();
    drawControlPanel();
    if (!view_.npcMenu && view_.dialogue.empty() && !view_.shopOpen) {
        painter_.label("D2X", 22, 20, 20, gold);
        painter_.label("CLASSIC ENGINE / C++", 72, 24, 10, {154, 149, 129, 255});
        painter_.label("LV " + std::to_string(sim.player.level) + "  XP " +
                           std::to_string(sim.player.experience),
                       22, 48, 10, {154, 149, 129, 255});
        int worldWidth = view_.inventory.open && !view_.inventory.storage ? int(inventoryBounds().x) : W;
        const auto &regionName = session_.region().definition.name;
        painter_.label(regionName, (worldWidth - painter_.measure(regionName, 20)) / 2, 20, 20, gold);
        const std::string subtitle = "ACT I  /  ORIGINAL MPQ ASSETS";
        painter_.label(subtitle, (worldWidth - painter_.measure(subtitle, 10)) / 2, 46, 10,
                       {137, 136, 112, 255});
        painter_.label("Ctrl+F2 Map   TAB Map", W - 204, 191, 10, {153, 144, 118, 255});
        painter_.label("Ctrl+F1 Help  M Sound", W - 204, 207, 10, {153, 144, 118, 255});
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
    for (const auto &status : session_.shrineStatuses()) {
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
                           "Space                      Toggle walk / run",
                           "Tab / Ctrl+F2              Automap / Map catalog",
                           "Ctrl+F3 / Ctrl+F4          Collision / Walk to stash",
                           "P / M                      Pause / Mute",
                           "R                          Restore life",
                           "F11 / Ctrl + F11           Save / Restart in town",
                           "F12                        Save screenshot",
                           "Ctrl+F1                   Close this panel"};
    for (int i = 0; i < int(std::size(lines)); i++)
        painter_.label(lines[i], W / 2 - 194, 216 + i * 23, 12,
                       i < 4 ? parchment : Color{150, 148, 132, 255});
    painter_.centered("Ctrl+Alt+G/E: gold/XP   A/T: reset points   C: next class   W: waypoints", 575, 11, gold);
}
void SceneView::draw(Vec mouse) const {
    const auto &map = session_.map();
    const auto &sim = session_.state();

    ClearBackground({10, 13, 11, 255});
    BeginScissorMode(0, 0, W, H - HUD);
    drawTerrain();
    if (view_.debug) {
        for (int y = 0; y < map.grid.height; y++)
            for (int x = 0; x < map.grid.width; x++) {
                auto p = screen({x + .5f, y + .5f});
                if (p.x < 0 || p.x > W || p.y < 0 || p.y > H - HUD)
                    continue;
                if (!map.grid.walkable(x, y))
                    diamond(p, 7, {213, 65, 42, 90});
            }
        for (auto p : sim.player.route)
            DrawCircleV(rv(screen(p)), 3, GREEN);
    }
    drawActors();
    drawMagic();
    drawLootLabels(mouse);
    drawExitHint(mouse);
    drawObjectHint(mouse);
    EndScissorMode();
    DrawRectangleGradientV(0, 0, W, 105, {0, 0, 0, 145}, {0, 0, 0, 0});
    if (!view_.inventory.open && !view_.npcMenu && view_.dialogue.empty())
        drawMinimap(false);
    if (view_.automap)
        drawMinimap(true);
    drawHud();
    if (!view_.blocksWorld() && !view_.inventory.open && mouse.y < H - HUD) {
        for (const auto &enemy : sim.area.enemies) {
            if (enemy.hp <= 0 || !session_.active(enemy.pos) ||
                (screen(enemy.pos) - Vec{0, 25} - mouse).length() >= 24)
                continue;
            const auto &identity = enemy.identity;
            auto title = identity.superUnique.empty() ? identity.monster : identity.superUnique;
            auto detail = std::string(monsterRankName(identity.rank));
            if (monsterImplementation(identity.monster).substitute)
                detail += " / Fallen substitute";
            painter_.centered(title, 76, 16, gold);
            painter_.centered(detail, 98, 10, parchment);
            break;
        }
    }
    drawStorage(mouse);
    drawCube(mouse);
    drawCharacter(mouse);
    drawSkillTree(mouse);
    drawInventory(mouse);
    drawBelt(mouse);
    if (view_.pause)
        painter_.centered("PAUSED", H / 2 - 40, 32, gold);
    if (sim.player.dead) {
        DrawRectangle(0, 100, W, 320, {0, 0, 0, 130});
        painter_.centered("YOU HAVE DIED", 250, 32, {187, 46, 30, 255});
        painter_.centered("Press R to restart this area", 300, 16);
    }
    if (view_.help)
        drawHelp();
    if (view_.travelMenu && view_.waypointSource)
        drawWaypointMenu(mouse);
    else if (view_.travelMenu) {
        DrawRectangle(0, 0, W, H, {0, 0, 0, 175});
        frame({W / 2.f - 345, 55, 690, 545});
        painter_.centered("ACT I MAP CATALOG", 76, 24, gold);
        painter_.centered("Developer catalog / Outdoors, caves and Tower connect in the world", 112, 14);
        const auto entries = travelEntries();
        int pages = (int(entries.size()) + worldPageSize - 1) / worldPageSize;
        for (int i = 0; i < worldPageSize && view_.travelPage * worldPageSize + i < int(entries.size());
             i++) {
            const auto &entry = entries[view_.travelPage * worldPageSize + i];
            Rectangle r = travelSlot(i);
            bool current = entry.destination && *entry.destination == session_.region().definition.id;
            frame(r, current ? gold : Color{70, 66, 53, 255});
            std::string title = (entry.level ? std::to_string(entry.level) + "  " : "") + entry.name;
            painter_.label(title, int(r.x) + 12, int(r.y) + 5, 16,
                           current             ? gold
                           : entry.destination ? parchment
                                               : Color{145, 139, 126, 255});
            auto status = entry.status;
            if (!entry.missing.empty())
                status += " (" + std::to_string(entry.missing.size()) + " missing files)";
            painter_.label(status, int(r.x) + 12, int(r.y) + 25, 12);
        }
        for (bool next : {false, true}) {
            auto r = travelPageButton(next);
            frame(r);
            painter_.label(next ? "NEXT >" : "< PREVIOUS", int(r.x) + 12, int(r.y) + 8, 14);
        }
        painter_.centered(
            std::to_string(view_.travelPage + 1) + " / " + std::to_string(pages) + "   PgUp / PgDn", 554, 14);
    }
    drawSkillControls(mouse);
    if (view_.npcMenu) drawNpcMenu();
    if (view_.shopOpen) drawNpcShop(mouse);
    if (!view_.dialogue.empty()) drawNpcDialogue();
    drawInventoryCursor(mouse);
    if (!assets_.cursor.frames.empty())
        sprite(assets_.cursor.frame(0, 0), mouse);
}
} // namespace d2x

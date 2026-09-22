#include "scene_view.hpp"
#include <algorithm>
namespace d2x {
void SceneView::orb(int x, int y, float value, Color light, Color dark, const std::string &text) const {

    (void)dark;
    if (auto sprite = assets_.orbs.frame(0, light.r > light.b ? 0 : 1)) {
        auto t = sprite->texture;
        Rectangle source{0, 0, float(t.width), float(t.height)};
        Rectangle bounds{x - 40.f, y - 40.f, 80, 80};
        DrawTexturePro(t, source, bounds, {0, 0}, 0, {24, 24, 24, 255});
        float fraction = std::clamp(value, 0.f, 1.f);
        if (fraction > 0) {
            source.y = t.height * (1 - fraction);
            source.height = t.height * fraction;
            bounds.y += 80 * (1 - fraction);
            bounds.height = 80 * fraction;
            DrawTexturePro(t, source, bounds, {0, 0}, 0, WHITE);
        }
    }
    painter_.label(text, x - painter_.measure(text, 10) / 2, y + 47, 10);
}
void SceneView::drawHud() const {
    const auto &sim = session_.state();

    DrawRectangleGradientV(0, H - HUD, W, HUD, {24, 24, 21, 255}, {7, 9, 10, 255});
    DrawLine(0, H - HUD, W, H - HUD, {131, 113, 79, 255});
    for (int i = 0; i < W; i += 32) {
        DrawLine(i, H - HUD + 5, i + 20, H - HUD + 5, {49, 45, 34, 255});
        DrawLine(i, H - 7, i + 20, H - 7, {48, 41, 28, 255});
    }
    orb(77, H - 59, sim.player.hp / playerRules().maxLife, {180, 20, 14, 255}, {47, 3, 3, 255},
        std::to_string(int(sim.player.hp)) + " / " + std::to_string(int(playerRules().maxLife)));
    orb(W - 77, H - 59, sim.player.mana / playerRules().maxMana, {21, 74, 199, 255}, {4, 7, 50, 255},
        std::to_string(int(sim.player.mana)) + " / " + std::to_string(int(playerRules().maxMana)));
    if (assets_.panel.frames.size() >= 5) {
        sprite(&assets_.panel.frames[0], {18, H - 104.f});
        sprite(&assets_.panel.frames[4], {W - 135.f, H - 104.f});
    }
    painter_.label("LIFE", 59, H - HUD + 7, 10, gold);
    painter_.label("MANA", W - 93, H - HUD + 7, 10, gold);
    int occupied = 0;
    const auto &inventory = session_.inventory();
    const auto &backpack = inventory.container(session_.playerContainers().backpack)->spec;
    const int capacity = backpack.columns * backpack.rows;
    for (auto id : inventory.contents(session_.playerContainers().backpack)) {
        const auto &definition = *inventory.catalog().find(inventory.item(id)->definition);
        occupied += definition.width * definition.height;
    }
    painter_.label("[I] BACKPACK " + std::to_string(occupied) + " / " + std::to_string(capacity), 738, H - 17,
                   10, occupied == capacity ? Color{220, 115, 95, 255} : gold);
    painter_.label("STAMINA", 738, H - 43, 10, {149, 142, 119, 255});
    DrawRectangle(797, H - 42, 110, 6, {42, 38, 22, 255});
    DrawRectangle(797, H - 42, int(110 * sim.player.stamina / playerRules().maxStamina), 6,
                  {163, 139, 66, 255});
    for (int i = 0; i < int(hotbarSlots); i++) {
        const auto &skill = skillDefinition(view_.hotbar[i]);
        float cooldown = sim.player.cooldown[size_t(skill.id)];
        auto cost = std::to_string(int(skill.manaCost)) + " MP";
        int x = int(skillSlot(i).x);
        frame({float(x), H - 94.f, 56, 60}, i == view_.selected ? gold : Color{65, 64, 56, 255});
        int original = skill.id == Skill::Whirlwind ? 50
                       : skill.id == Skill::Leap    ? 34
                       : skill.id == Skill::WarCry  ? 56
                                                    : -1;
        if (original >= 0 && size_t(original) < assets_.barbarianIcons.frames.size()) {
            auto t = assets_.barbarianIcons.frames[original].texture;
            DrawTexturePro(t, {0, 0, float(t.width), float(t.height)}, {x + 3.f, H - 91.f, 50, 50}, {0, 0}, 0,
                           WHITE);
        } else
            icon(int(skill.id), {x + 28.f, H - 66.f}, i == view_.selected, sim.time);
        painter_.label("F" + std::to_string(i + 5), x + 7, H - 89, 10, gold);
        if (cooldown > 0) {
            DrawRectangle(x + 5, H - 90, 52, 52, {0, 0, 0, 155});
            painter_.label(TextFormat("%.1f", cooldown), x + 22, H - 70, 14);
        }
        painter_.label(skill.shortName, x + (62 - painter_.measure(skill.shortName, 8)) / 2, H - 28, 8,
                       {172, 162, 138, 255});
        painter_.label(cost, x + 18, H - 16, 8, {92, 126, 174, 255});
    }
    painter_.label("MONSTERS SLAIN", 738, H - HUD + 19, 10, {132, 128, 111, 255});
    painter_.label(std::to_string(sim.area.kills) + " / " + std::to_string(sim.area.enemies.size()), 738,
                   H - HUD + 38, 24, gold);
    painter_.label(sim.player.running ? "RUN  [SPACE]" : "WALK [SPACE]", 738, H - 30, 10,
                   {169, 157, 121, 255});
    painter_.label("D2X", 22, 20, 20, gold);
    painter_.label("CLASSIC ENGINE / C++", 72, 24, 10, {154, 149, 129, 255});
    int worldWidth = view_.inventory.open && !view_.inventory.storage ? int(inventoryBounds().x) : W;
    const auto &regionName = session_.region().definition.name;
    painter_.label(regionName, (worldWidth - painter_.measure(regionName, 20)) / 2, 20, 20, gold);
    const std::string subtitle = "ACT I  /  ORIGINAL MPQ ASSETS";
    painter_.label(subtitle, (worldWidth - painter_.measure(subtitle, 10)) / 2, 46, 10, {137, 136, 112, 255});
    painter_.label("F2 Catalog   TAB Map", W - 204, 191, 10, {153, 144, 118, 255});
    painter_.label("F1 Help     M Sound", W - 204, 207, 10, {153, 144, 118, 255});
    if (!sim.message.empty())
        painter_.centered(sim.message, H - HUD - 35, 16, {218, 176, 95, 255});
    if (!view_.help)
        painter_.label(
            "LMB Move / Attack / Pick up   ALT Item names   I Inventory   1-4 Potions   F5-F10 Skills", 23,
            H - HUD - 22, 10, {188, 181, 158, 255});
    if (!view_.dialogue.empty()) {
        frame({W / 2.f - 285, H - HUD - 97.f, 570, 52});
        painter_.centered(view_.dialogue, H - HUD - 78, 14, gold);
    }
    if (view_.noticeTime > 0) {
        int width = painter_.measure(view_.lootNotice, 14) + 32;
        float centerX =
            view_.inventory.open && !view_.inventory.storage ? inventoryBounds().x * .5f : W * .5f;
        frame({centerX - width * .5f, H - HUD - 139.f, float(width), 34},
              view_.noticeError ? Color{190, 91, 67, 255} : gold);
        painter_.label(view_.lootNotice, int(centerX - (width - 32) * .5f), H - HUD - 129, 14,
                       view_.noticeError ? Color{245, 166, 135, 255} : parchment);
    }
}
void SceneView::drawHelp() const {

    DrawRectangle(0, 0, W, H, {0, 0, 0, 155});
    frame({W / 2.f - 260, 90, 520, 510});
    painter_.centered("FIELD MANUAL", 140, 25, gold);
    painter_.centered("Diablo II classic resource simulation", 178, 12);
    const char *lines[] = {"Left click / hold          Move / Attack / Talk / Pick up",
                           "Hold Alt                   Show ground item names",
                           "I                          Open / close inventory",
                           "W A S D                    Move in screen directions",
                           "Right click                Cast selected skill",
                           "1 through 4 / B            Drink belt potion / Expand belt",
                           "F5 through F10             Cast skill at cursor",
                           "Space                      Toggle walk / run",
                           "Tab / F2                   Automap / Map catalog",
                           "F3 / F4                    Collision / Walk to stash",
                           "P / M                      Pause / Mute",
                           "R                          Restore life",
                           "F11 / Ctrl + F11           Save game / Load game",
                           "F12                        Save screenshot",
                           "F1                         Close this panel"};
    for (int i = 0; i < int(std::size(lines)); i++)
        painter_.label(lines[i], W / 2 - 194, 216 + i * 23, 12,
                       i < 4 ? parchment : Color{150, 148, 132, 255});
    painter_.centered("Monster loot awaits original drop rules.", 575, 12, gold);
}
void SceneView::draw(Vec mouse) const {
    const auto &map = session_.map();
    const auto &sim = session_.state();

    ClearBackground({10, 13, 11, 255});
    BeginScissorMode(0, 0, W, H - HUD);
    drawTerrain();
    if (view_.clickAge < .8f)
        diamond(screen(view_.clickAt), 10 + view_.clickAge * 12, Fade(gold, 1 - view_.clickAge));
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
    EndScissorMode();
    DrawRectangleGradientV(0, 0, W, 105, {0, 0, 0, 145}, {0, 0, 0, 0});
    if (!view_.inventory.open)
        drawMinimap(false);
    if (view_.automap)
        drawMinimap(true);
    drawHud();
    if (!view_.blocksWorld() && !view_.inventory.open && mouse.y < H - HUD) {
        for (const auto &enemy : sim.area.enemies) {
            if (enemy.hp <= 0 || (screen(enemy.pos) - Vec{0, 25} - mouse).length() >= 24)
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
    if (view_.travelMenu) {
        DrawRectangle(0, 0, W, H, {0, 0, 0, 175});
        frame({W / 2.f - 345, 55, 690, 545});
        painter_.centered("ACT I MAP CATALOG", 76, 24, gold);
        painter_.centered("Select available terrain / original level links pending", 112, 14);
        const auto &entries = session_.worldEntries();
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
    if (mouse.y >= H - HUD && !view_.help && !view_.travelMenu && !view_.inventory.drag) {
        for (int i = 0; i < int(hotbarSlots); i++)
            if (CheckCollisionPointRec(rv(mouse), skillSlot(i))) {
                const auto &skill = skillDefinition(view_.hotbar[i]);
                frame({W / 2.f - 230, H - HUD - 85.f, 460, 65});
                painter_.centered(skill.name, H - HUD - 72, 18, gold);
                painter_.centered(skill.description, H - HUD - 45, 12);
            }
    }
    drawInventoryCursor(mouse);
    if (!assets_.cursor.frames.empty())
        sprite(assets_.cursor.frame(0, 0), mouse);
}
} // namespace d2x

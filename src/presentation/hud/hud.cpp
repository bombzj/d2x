#include "presentation/world/palette_blend_view.hpp"
#include "presentation/scene_view.hpp"
#include "quest_panel.hpp"
#include "client/actor_client.hpp"
#include "core/text.hpp"
#include "content/world/world_catalog.hpp"
#include <algorithm>
#include <cmath>
namespace d2x {
int SceneView::gameMenuItemCount() const { return assets_.escapeMenu.count(view_.gameMenuPage); }
Rectangle SceneView::gameMenuItemBounds(int index) const { return assets_.escapeMenu.bounds(view_.gameMenuPage, index); }
bool SceneView::gameMenuItemEnabled(int index) const { return assets_.escapeMenu.enabled(view_.gameMenuPage, index); }
int SceneView::gameMenuAt(Vec mouse) const { return assets_.escapeMenu.hit(view_.gameMenuPage, mouse); }
void SceneView::applyAudioSettings(const AudioSettings &settings) {
    const bool changed = view_.audio != settings;
    view_.audio = settings;
    assets_.audio.setVolumes(settings.sound, settings.music);
    assets_.streams.settings(settings);
    // Native option value changes use Sounds 1 (cursor_pass), after applying
    // the new master volume. Loading preferences is silent.
    if (changed && view_.gameMenuOpen && view_.gameMenuPage == EscapeMenuPage::Sound)
        assets_.sceneAudio.play("cursor_pass", 0);
}
bool SceneView::adjustGameMenuSound(const FrameInput &input) {
    if (view_.gameMenuPage != EscapeMenuPage::Sound) {
        view_.gameMenuSliderDragging = -1;
        return false;
    }
    bool consumed = false;
    auto settings = view_.audio;
    if (input.insideViewport && input.leftPressed) {
        for (int row = 0; row < 2; ++row)
            if (assets_.escapeMenu.sliderHit(row, input.mouse)) view_.gameMenuSliderDragging = row;
    }
    if (view_.gameMenuSliderDragging >= 0) {
        view_.gameMenuSelected = view_.gameMenuSliderDragging;
        consumed = true;
        if (input.leftPressed || input.leftHeld || input.leftReleased) {
            const int value = assets_.escapeMenu.sliderValue(input.mouse.x);
            (view_.gameMenuSliderDragging == 0 ? settings.sound : settings.music) = value;
        }
        if (!input.leftHeld || input.leftReleased) view_.gameMenuSliderDragging = -1;
    } else if (view_.gameMenuSelected < 2 && (input.menuLeft || input.menuRight)) {
        auto &value = view_.gameMenuSelected == 0 ? settings.sound : settings.music;
        value = std::clamp(value + (input.menuRight ? 5 : -5), 0, 100);
        consumed = true;
    }
    if (settings != view_.audio) applyAudioSettings(settings);
    return consumed;
}
void SceneView::scrollGameMenu(const FrameInput &input) {
    view_.gameMenuScrollPressed = -1;
    if (view_.gameMenuPage != EscapeMenuPage::Controls) return;
    const int limit = assets_.escapeMenu.controlScrollLimit();
    const auto track = assets_.escapeMenu.controlTrack();
    int delta = -int(input.wheel) + input.menuDelta + input.pageDelta * 14;
    if (input.entryHome) view_.gameMenuControlScroll = 0;
    if (input.entryEnd) view_.gameMenuControlScroll = limit;
    const Rectangle up{track.x, track.y - 13, track.width, 13};
    const Rectangle down{track.x, track.y + track.height, track.width, 13};
    if (input.insideViewport && input.leftHeld) {
        if (CheckCollisionPointRec(rv(input.mouse), up)) view_.gameMenuScrollPressed = 0;
        else if (CheckCollisionPointRec(rv(input.mouse), down)) view_.gameMenuScrollPressed = 1;
    }
    if (input.insideViewport && input.leftPressed) {
        if (CheckCollisionPointRec(rv(input.mouse), up)) --delta;
        else if (CheckCollisionPointRec(rv(input.mouse), down)) ++delta;
        else if (CheckCollisionPointRec(rv(input.mouse), track)) view_.gameMenuScrollDragging = true;
    }
    if (view_.gameMenuScrollDragging && input.leftHeld) {
        const float height = float(assets_.escapeMenu.controlThumbHeight());
        const auto fraction = std::clamp((input.mouse.y - track.y - height / 2) / (track.height - height), 0.f, 1.f);
        view_.gameMenuControlScroll = int(std::round(fraction * limit));
    } else view_.gameMenuControlScroll = std::clamp(view_.gameMenuControlScroll + delta, 0, limit);
    if (!input.leftHeld || input.leftReleased) view_.gameMenuScrollDragging = false;
}
void SceneView::drawGameMenu() const {
    if (!view_.gameMenuOpen) return;
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
    if (!blend) return;
    blend->drawRectangle({0, 0, float(W), float(H)}, BLACK);
    const std::array values{view_.automapLarge ? 0 : 1,
        view_.automapFade == AutomapFade::No ? 0 : view_.automapFade == AutomapFade::Center ? 1 :
        view_.automapFade == AutomapFade::Everything ? 2 : 3,
        int(view_.automapCenterWhenCleared), int(view_.automapParty), int(view_.automapNames)};
    assets_.escapeMenu.draw(view_.gameMenuPage, view_.gameMenuSelected, view_.gameMenuTime,
                            values, view_.audio, view_.lighting, view_.gameMenuControlScroll, view_.gameMenuScrollPressed, *blend);
}
void SceneView::drawHelp() const {

    DrawRectangle(0, 0, W, H, {0, 0, 0, 155});
    frame({W / 2.f - 260, 90, 520, 510});
    painter_.centered("FIELD MANUAL", 140, 25, gold);
    painter_.centered("Diablo II online controls", 178, 12);
    const char *lines[] = {"Left click / hold          Move / Attack / Talk / Pick up",
                           "Hold Alt                   Show ground item names",
                           "I / A / T                  Inventory / Character / Skills",
                           "Arrow keys                 Pan automap",
                           "Right click / S            Cast / Open skill selection",
                           "1 through 4 / ~            Drink belt potion / Expand belt",
                           "Hover skill, F1-F8         Bind selected mouse skill",
                           "F1 through F8              Select bound mouse skill",
                           "R                          Toggle walk / run",
                           "Tab / V                    Automap / Switch map side",
                           "W / O / P                  Weapon swap / Hireling / Party",
                           "Enter / M                  Chat / Message log",
                           "Q / Home                   Quest log / Center map",
                           "F12 / Ctrl+F12             Map names / Screenshot",
                           "Ctrl+F1                   Close this panel"};
    for (int i = 0; i < int(std::size(lines)); i++)
        painter_.label(lines[i], W / 2 - 194, 216 + i * 23, 12,
                       i < 4 ? parchment : Color{150, 148, 132, 255});
}
void SceneView::drawMonsterCorpseName(const std::string &name) const {
    // Retail D2Client C0986–C0B66: red Font16 at baseline 32, then skip
    // the life bar and modifiers. Keep the original font palette.
    const auto &font=assets_.characterRedFont;
    const auto codes=utf8Decode(name);
    int width=0;
    for(const auto code:codes) width+=font.widthOf(code);
    const auto viewport=worldViewport();
    int x=int(viewport.x+viewport.width/2)-width/2;
    for(const auto code:codes) {
        if(const auto *image=font.glyphOf(code))
            DrawTexturePro(image->texture,{0,0,float(image->texture.width),float(image->texture.height)},
                {float(x+image->x),float(32+image->y-image->texture.height),
                 float(image->texture.width),float(image->texture.height)},{},0,WHITE);
        x+=font.widthOf(code);
    }
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
    const auto actor = actorClient_.controlledActor();
    const auto nativeCell = [](const Sprite *image, int x, int baseline) {
        if (image) DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
            {float(x + image->x), float(baseline + image->y - image->texture.height),
             float(image->texture.width), float(image->texture.height)}, {}, 0, WHITE);
    };
    const auto artwork = [&](const GpuAnimation &art, int baseline) {
        int width = 0;
        for (const auto &cell : art.frames) width += cell.texture.width;
        int x = W / 2 - width / 2 - 1; // D2Win #10138's centered DC6 cells.
        for (const auto &cell : art.frames) { nativeCell(&cell, x, baseline); x += 256; }
    };
    const auto text = [&](const std::string &line, int baseline) {
        const auto codes = utf8Decode(line);
        int width = 0;
        for (const auto code : codes) width += assets_.deathFont.widthOf(code);
        // D2Win #10150 reserves eight extra pixels and centers text in them.
        int x = W / 2 - width / 2 + 4;
        for (const auto code : codes) {
            nativeCell(assets_.deathFont.glyphOf(code), x, baseline);
            x += assets_.deathFont.widthOf(code);
        }
    };
    // D2Client 1.13c BEE60: native baselines, no screen-sized dark overlay.
    artwork(assets_.deathTitle, H / 2 - 94);
    artwork(assets_.deathInstruction, H / 2 - 46);
    int baseline = H / 2 - 46;
    if (actor.hardcore == true) text(assets_.characterLabels.at("Harddeathmessage"), baseline + 48);
    else if (actor.hardcore == false) {
        if (actor.deathGoldLost && *actor.deathGoldLost) {
            auto line = assets_.characterLabels.at("Deathmessage");
            const auto amount = line.find("%d");
            if (amount != std::string::npos) {
                line.replace(amount, 2, std::to_string(*actor.deathGoldLost));
                text(line, baseline += 48);
            }
        }
        if (actor.difficulty && *actor.difficulty > 0)
            text(assets_.characterLabels.at("Deathmessnight"), baseline + 48);
    }
}
float SceneView::partyPortraitOffset() const {
    return view_.portraitsVisible && worldViewport().x == 0 && !view_.capturesWorldInput() ?
        float(party_.portraitCount()) * 56 * classicPanelScale() : 0;
}
void SceneView::drawBhOverlay() const {
    if (view_.capturesWorldInput()) return;
    // Area name: top-left, matching BH's display_level_names.
    if (view_.bhShowAreaName) {
        if (const auto &level = assets_.worldLevel(int(mapView().region)); !level.name.empty()) {
            DrawRectangle(4, 4, painter_.measure(level.name, 14) + 12, 22, {0, 0, 0, 160});
            painter_.label(level.name, 10, 7, 14, parchment);
        }
    }
    // Experience text: above the classic XP bar, showing current/needed/percent.
    if (view_.bhShowXpText) {
        const auto &player = characterView_;
        if (!player.unknownStats.contains("experience") && !player.unknownStats.contains("level") &&
            player.nextLevelExperience && *player.nextLevelExperience > player.currentLevelExperience) {
            const auto gained = player.experience > player.currentLevelExperience
                ? player.experience - player.currentLevelExperience : 0;
            const auto needed = *player.nextLevelExperience - player.currentLevelExperience;
            const int percent = int(std::clamp(double(gained) / double(needed) * 100.0, 0.0, 100.0));
            const auto text = std::to_string(player.level) + "  " +
                std::to_string(gained) + "/" + std::to_string(needed) + " (" + std::to_string(percent) + "%)";
            const int width = painter_.measure(text, 12);
            const int x = (W - width) / 2;
            const int y = H - HUD - 18;
            DrawRectangle(x - 4, y - 2, width + 8, 16, {0, 0, 0, 160});
            painter_.label(text, x, y, 12, parchment);
        }
    }
    // Belt status: compact potion counts above the classic belt row.
    if (view_.bhShowBeltStatus) {
        const auto *belt = inventoryView_.container(inventoryView_.containers.belt);
        if (belt) {
            struct PotionCount { std::string name; int count = 0; Color color; };
            std::vector<PotionCount> potions;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row) {
                    const auto *item = inventoryView_.item(inventoryView_.itemAt(belt->id, {column, row}));
                    if (!item || item->definition == "gld") continue;
                    const auto type = inventoryView_.definition(item->definition);
                    const auto label = item->name.empty() ? item->definition : item->name;
                    auto found = std::find_if(potions.begin(), potions.end(),
                        [&](const PotionCount &entry) { return entry.name == label; });
                    if (found == potions.end()) {
                        Color color = parchment;
                        if (type && type->beltPotionType == "hp") color = {255, 60, 60, 255};
                        else if (type && type->beltPotionType == "mp") color = {60, 120, 255, 255};
                        else if (type && type->beltPotionType == "rv") color = {255, 60, 255, 255};
                        potions.push_back({label, 1, color});
                    } else ++found->count;
                }
            if (!potions.empty()) {
                int x = 8;
                const int y = H - HUD - 40;
                for (const auto &potion : potions) {
                    const auto text = potion.name + " x" + std::to_string(potion.count);
                    const int width = painter_.measure(text, 12);
                    DrawRectangle(x - 2, y, width + 8, 16, {0, 0, 0, 160});
                    painter_.label(text, x + 2, y + 1, 12, potion.color);
                    x += width + 14;
                }
            }
        }
    }
}
void SceneView::drawBhSettings() const {
    if (!view_.bhSettingsOpen) return;
    DrawRectangle(0, 0, W, H, {0, 0, 0, 140});
    const int width = 340;
    const int rows = 13;
    const int rowHeight = 24;
    const int panelHeight = 44 + rows * rowHeight;
    const Rectangle panel{float((W - width) / 2), float((H - panelHeight) / 2), float(width), float(panelHeight)};
    DrawRectangleRec(panel, {18, 18, 22, 245});
    DrawRectangleLinesEx(panel, 1, {90, 80, 60, 255});
    const auto title = "BH Display Settings";
    painter_.centered(title, int(panel.y + 12), 16, gold);
    struct Entry { const char *label; bool value; };
    const Entry entries[] = {
        {"Reveal Map", view_.bhRevealMap},
        {"Always Show Loot", view_.bhAlwaysShowLoot},
        {"Area Name", view_.bhShowAreaName},
        {"XP Text", view_.bhShowXpText},
        {"Belt Status", view_.bhShowBeltStatus},
        {"Full Light Radius", view_.bhForceLightRadius},
        {"Path Line", view_.bhShowPathLine},
        {"Monster Resistances", view_.bhShowResistances},
        {"Show Monsters", view_.bhShowMonsters},
        {"Monster Enchantments", view_.bhShowEnchantments},
        {"Show Chests", view_.bhShowChests},
        {"Automap On Join", view_.bhAutomapOnJoin},
    };
    const int x = int(panel.x) + 20;
    int y = int(panel.y) + 44;
    for (int i = 0; i < int(std::size(entries)); ++i) {
        const bool selected = i == view_.bhSettingsSelected;
        if (selected) {
            DrawRectangle(x - 8, y - 3, width - 24, rowHeight - 2, {60, 50, 30, 220});
        }
        painter_.label(entries[i].label, x, y, 14, selected ? gold : parchment);
        const char *state = entries[i].value ? "ON" : "OFF";
        const int stateWidth = painter_.measure(state, 14);
        painter_.label(state, int(panel.x + panel.width) - stateWidth - 24, y, 14,
                       entries[i].value ? Color{100, 220, 100, 255} : Color{180, 80, 80, 255});
        y += rowHeight;
    }
    const char *hint = "Up/Down: select   Enter: toggle   Esc: close";
    painter_.label(hint, x, y + 4, 12, Color{150, 140, 120, 255});
}
void SceneView::drawUi(Vec mouse) const {
    deferredTooltip_.active = false;
    drawControlPanel();
    drawDurabilityWarning();
    drawHostilityWarning();
    drawQuestNotice();
    if (view_.portraitsVisible && worldViewport().x == 0 && !view_.capturesWorldInput()) {
        const int palette = std::clamp(mapView().palette, 0, 4);
        const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
        if (blend) party_.drawPortraits(15, *blend);
    }
    drawHirelingPortrait(mouse);
    if (view_.noticeTime > 0)
        painter_.centered(view_.lootNotice, H - HUD - 35, 14, view_.noticeError ? RED : parchment);
    drawStorage(mouse);
    drawCube(mouse);
    drawOrifice(mouse);
    drawPlayerTrade(mouse);
    drawCharacter(mouse);
    drawHireling(mouse);
    drawQuests(mouse);
    if (view_.partyOpen) { drawPanelFrame(false); party_.draw(characterView_.name); }
    drawSkillTree(mouse);
    if (view_.shopOpen) drawNpcShop(mouse);
    drawInventory(mouse);
    drawBelt(mouse);
    if (view_.help)
        drawHelp();
    if (view_.travelMenu && view_.waypointSource)
        drawWaypointMenu(mouse);
    drawSkillControls(mouse);
    if (view_.npcMenu) drawNpcMenu(mouse);
    drawHirelingList(mouse);
    if (!view_.dialogue.empty()) drawNpcDialogue();
    if (!view_.playerTradeBlocking) chat_.draw(worldViewport());
    const bool itemCursor = drawInventoryCursor(mouse);
    drawGameMenu();
    tradeInvite_.draw(worldViewport());
    tradeInvite_.drawGoldDialog();
    if (deferredTooltip_.active && !view_.inventory.goldDialog && !view_.gameMenuOpen)
        drawItemText(deferredTooltip_.lines, deferredTooltip_.quality, deferredTooltip_.anchor,
                     deferredTooltip_.price, deferredTooltip_.priceLabel);
    drawBhOverlay();
    drawBhSettings();
    drawDeathNotice(); // Native C3DAA draws death notices after the main HUD.
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

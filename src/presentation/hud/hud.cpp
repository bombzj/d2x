#include "presentation/world/palette_blend_view.hpp"
#include "presentation/scene_view.hpp"
#include "quest_panel.hpp"
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
                            values, view_.audio, view_.gameMenuControlScroll, view_.gameMenuScrollPressed, *blend);
}
void SceneView::drawHelp() const {

    DrawRectangle(0, 0, W, H, {0, 0, 0, 155});
    frame({W / 2.f - 260, 90, 520, 510});
    painter_.centered("FIELD MANUAL", 140, 25, gold);
    painter_.centered("Diablo II online controls", 178, 12);
    const char *lines[] = {"Left click / hold          Move / Attack / Talk / Pick up",
                           "Hold Alt                   Show ground item names",
                           "I / A / S                  Inventory / Character / Skills",
                           "Arrow keys                 Pan automap",
                           "Right click                Cast; click slot to choose",
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
float SceneView::partyPortraitOffset() const {
    return view_.portraitsVisible && worldViewport().x == 0 && !view_.capturesWorldInput() ?
        float(party_.portraitCount()) * 56 * classicPanelScale() : 0;
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
    drawDeathNotice();
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

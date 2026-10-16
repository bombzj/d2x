#include "input.hpp"
#include "presentation/graphics/primitives.hpp"
#include <algorithm>

namespace d2x {
Viewport currentViewport() {
    return {1.f, {0.f, 0.f}};
}
FrameInput pollInput(const Viewport &viewport) {
    FrameInput input;
    const auto mouse = GetMousePosition();
    input.mouse = {mouse.x, mouse.y};
    input.insideViewport = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                           IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
                           input.mouse.x >= viewport.offset.x &&
                           input.mouse.y >= viewport.offset.y &&
                           input.mouse.x < viewport.offset.x + W * viewport.scale &&
                           input.mouse.y < viewport.offset.y + H * viewport.scale;
    if (input.insideViewport) {
        input.mouse = (Vec{mouse.x, mouse.y} - viewport.offset) * (1 / viewport.scale);
        input.leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        input.rightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        input.leftHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        input.rightHeld = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        input.mouseWheel = GetMouseWheelMove();
    }
    if (!input.leftHeld && !input.rightHeld) {
        input.leftReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        input.rightReleased = IsMouseButtonReleased(MOUSE_BUTTON_RIGHT);
    }
    if (IsWindowFocused()) {
        input.focused = true;
        input.control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        input.alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        input.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        input.escape = IsKeyPressed(KEY_ESCAPE);
        input.enter = IsKeyPressed(KEY_ENTER);
        input.backspace = IsKeyPressed(KEY_BACKSPACE);
        input.tab = IsKeyPressed(KEY_TAB);
        input.chat = IsKeyPressed(KEY_ENTER);
        input.clear = IsKeyPressed(KEY_DELETE);
        input.end = IsKeyPressed(KEY_END);
        input.save = input.control && IsKeyPressed(KEY_F11);
        input.reload = input.control && input.shift && IsKeyPressed(KEY_F11);
        input.debugNpc = input.control && IsKeyPressed(KEY_N);
        input.debugWaypoint = input.control && IsKeyPressed(KEY_W);
        input.debugLoadout = input.control && IsKeyPressed(KEY_L);
        input.debugHitpoints = input.control && IsKeyPressed(KEY_H);
        input.debugMenu = input.control && IsKeyPressed(KEY_M);
        input.debugSkillTree = input.control && IsKeyPressed(KEY_T);
        input.debugExperience = input.control && IsKeyPressed(KEY_X);
        input.debugMap = input.control && IsKeyPressed(KEY_A);
        input.debugCombat = input.control && IsKeyPressed(KEY_C);
        input.debugSkills = input.control && IsKeyPressed(KEY_S);
        input.debugCharacter = input.control && IsKeyPressed(KEY_P);
        input.debugItem = input.control && IsKeyPressed(KEY_I);
        input.debugPet = input.control && IsKeyPressed(KEY_K);
        input.debugOnline = input.control && IsKeyPressed(KEY_O);
        input.debugHost = input.control && IsKeyPressed(KEY_B);
        input.debugQuit = input.control && IsKeyPressed(KEY_Q);
        input.capture = input.control && IsKeyPressed(KEY_R);
        input.pause = IsKeyPressed(KEY_PAUSE);
        input.menuUp = int(IsKeyPressed(KEY_UP));
        input.menuDown = int(IsKeyPressed(KEY_DOWN));
        input.menuLeft = int(IsKeyPressed(KEY_LEFT));
        input.menuRight = int(IsKeyPressed(KEY_RIGHT));
        input.menuSelect = int(IsKeyPressed(KEY_ENTER)) + int(IsKeyPressed(KEY_SPACE));
        input.menuCancel = int(IsKeyPressed(KEY_ESCAPE));
        input.menuDelta = int(IsKeyPressed(KEY_DOWN)) - int(IsKeyPressed(KEY_UP));
        input.weaponSwap = !input.control && IsKeyPressed(KEY_W);
        // Classic gameplay shortcuts; the in-game help panel lists this table.
        // Ctrl/Alt keep their debug and modifier roles, so plain letters drive
        // the panels the same way the debug ui-input keys do.
        const auto plain = !input.control && !input.alt;
        input.inventory = plain && IsKeyPressed(KEY_I);
        input.character = plain && IsKeyPressed(KEY_A);
        input.skillTree = plain && IsKeyPressed(KEY_S);
        input.quests = plain && IsKeyPressed(KEY_Q);
        input.hireling = plain && IsKeyPressed(KEY_O);
        input.party = plain && IsKeyPressed(KEY_P);
        input.run = plain && IsKeyPressed(KEY_R);
        input.expandBelt = plain && IsKeyPressed(KEY_B);
        input.minimapSide = plain && IsKeyPressed(KEY_V);
        input.automap = input.tab;
        input.automapCenter = IsKeyPressed(KEY_HOME);
        input.automapNames = plain && IsKeyPressed(KEY_F12);
        input.screenshot = input.control && IsKeyPressed(KEY_F12);
        input.help = input.control && IsKeyPressed(KEY_F1);
        input.showLoot = input.alt;
        for (int i = 0; i < int(input.belt.size()); ++i)
            input.belt[size_t(i)] = plain && IsKeyPressed(KEY_ONE + i);
        for (size_t i = 0; i < input.skills.size(); ++i)
            input.skills[i] = plain && IsKeyPressed(KEY_F1 + int(i));
    } else {
        input.focused = false;
        input.insideViewport = false;
        input.leftHeld = input.rightHeld = false;
    }
    return input;
}
} // namespace d2x

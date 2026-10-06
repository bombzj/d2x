#include "input.hpp"
#include "presentation/graphics/primitives.hpp"
#include <algorithm>

namespace d2x {
Viewport currentViewport() {
    float scale = std::max(.001f, std::min(float(GetScreenWidth()) / W, float(GetScreenHeight()) / H));
    return {scale, {(GetScreenWidth() - W * scale) * .5f, (GetScreenHeight() - H * scale) * .5f}};
}
FrameInput pollInput(const Viewport &viewport) {
    FrameInput input;
    auto mouse = GetMousePosition();
    input.mouse = (Vec{mouse.x, mouse.y} - viewport.offset) * (1 / viewport.scale);
    input.insideViewport = input.mouse.x >= 0 && input.mouse.x < W && input.mouse.y >= 0 && input.mouse.y < H;
    input.leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    input.leftHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    input.leftReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    input.rightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    input.rightHeld = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    input.inventory = IsKeyPressed(KEY_I);
    const bool characterC = IsKeyPressed(KEY_C);
    const bool characterA = IsKeyPressed(KEY_A);
    const bool skillT = IsKeyPressed(KEY_T);
    const bool skillS = IsKeyPressed(KEY_S);
    input.character = characterA || characterC;
    input.skillTree = skillS || skillT;
    input.quests = IsKeyPressed(KEY_Q);
    input.hireling = IsKeyPressed(KEY_O);
    input.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    input.control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    input.storage = input.control && IsKeyPressed(KEY_F4);
    input.enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    input.backspace = IsKeyPressed(KEY_BACKSPACE);
    for (int character = GetCharPressed(); character; character = GetCharPressed())
        if (character >= '0' && character <= '9')
            input.text.push_back(char(character));
    input.quantityDelta =
        int(GetMouseWheelMove()) + int(IsKeyPressed(KEY_RIGHT)) - int(IsKeyPressed(KEY_LEFT));
    input.focused = IsWindowFocused();
    input.showLoot = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (input.control && input.showLoot) {
        input.debugGold = IsKeyPressed(KEY_G);
        input.debugCube = IsKeyPressed(KEY_B);
        input.debugExperience = IsKeyPressed(KEY_E);
        input.debugAttributes = characterA;
        input.debugTalents = skillT;
        input.debugWaypoints = IsKeyPressed(KEY_W);
        input.character = false;
        input.skillTree = false;
    }
    input.movement = {float(IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT)),
                      float(IsKeyDown(KEY_DOWN) - IsKeyDown(KEY_UP))};
    if (input.control && input.showLoot)
        input.movement = {};
    input.help = input.control && IsKeyPressed(KEY_F1);
    input.automap = IsKeyPressed(KEY_TAB);
    input.minimapSide = IsKeyPressed(KEY_V);
    input.automapCenter = IsKeyPressed(KEY_HOME);
    input.automapNames = !input.control && IsKeyPressed(KEY_F12);
    input.pageDelta = int(IsKeyPressed(KEY_PAGE_DOWN)) - int(IsKeyPressed(KEY_PAGE_UP));
    input.menuDelta = int(IsKeyPressed(KEY_DOWN)) - int(IsKeyPressed(KEY_UP));
    input.collision = input.control && IsKeyPressed(KEY_F3);
    input.run = !input.control && IsKeyPressed(KEY_R);
    input.escape = IsKeyPressed(KEY_ESCAPE);
    input.screenshot = input.control && IsKeyPressed(KEY_F12);
    input.save = IsKeyPressed(KEY_F11) && !input.control;
    input.load = IsKeyPressed(KEY_F11) && input.control;
    for (int i = 0; i < int(hotbarSlots); ++i)
        input.skills[i] = !input.control && IsKeyPressed(KEY_F1 + i);
    for (int i = 0; i < 4; ++i)
        input.belt[i] = IsKeyPressed(KEY_ONE + i);
    input.expandBelt = !input.control && IsKeyPressed(KEY_B);
    input.weaponSwap = !input.control && IsKeyPressed(KEY_W);
    return input;
}
} // namespace d2x

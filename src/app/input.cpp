#include "input.hpp"
#include "presentation/primitives.hpp"
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
    input.inventory = IsKeyPressed(KEY_I);
    input.storage = IsKeyPressed(KEY_F4);
    input.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    input.control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    input.enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    input.quantityDelta =
        int(GetMouseWheelMove()) + int(IsKeyPressed(KEY_RIGHT)) - int(IsKeyPressed(KEY_LEFT));
    input.focused = IsWindowFocused();
    input.showLoot = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    input.rightHeld = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    input.movement = {float(IsKeyDown(KEY_D) - IsKeyDown(KEY_A)), float(IsKeyDown(KEY_S) - IsKeyDown(KEY_W))};
    input.help = IsKeyPressed(KEY_F1);
    input.automap = IsKeyPressed(KEY_TAB);
    input.travel = IsKeyPressed(KEY_F2);
    input.pageDelta = int(IsKeyPressed(KEY_PAGE_DOWN)) - int(IsKeyPressed(KEY_PAGE_UP));
    input.collision = IsKeyPressed(KEY_F3);
    input.pause = IsKeyPressed(KEY_P);
    input.mute = IsKeyPressed(KEY_M);
    input.run = IsKeyPressed(KEY_SPACE);
    input.restart = IsKeyPressed(KEY_R);
    input.escape = IsKeyPressed(KEY_ESCAPE);
    input.screenshot = IsKeyPressed(KEY_F12);
    input.save = IsKeyPressed(KEY_F11) && !input.control;
    input.load = IsKeyPressed(KEY_F11) && input.control;
    for (int i = 0; i < int(hotbarSlots); ++i)
        input.skills[i] = IsKeyPressed(KEY_F5 + i);
    for (int i = 0; i < 4; ++i)
        input.belt[i] = IsKeyPressed(KEY_ONE + i);
    input.expandBelt = IsKeyPressed(KEY_B);
    return input;
}
} // namespace d2x

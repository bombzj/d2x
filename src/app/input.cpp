#include "input.hpp"
#include "presentation/graphics/primitives.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
namespace {
uint16_t controlKey(int key) {
    if ((key >= KEY_ZERO && key <= KEY_NINE) || (key >= KEY_A && key <= KEY_Z)) return uint16_t(key);
    if (key >= KEY_F1 && key <= KEY_F12) return uint16_t(0x70 + key - KEY_F1);
    if (key >= KEY_KP_0 && key <= KEY_KP_9) return uint16_t(0x60 + key - KEY_KP_0);
    constexpr std::pair<int, uint16_t> special[]{
        {KEY_BACKSPACE, 8}, {KEY_TAB, 9}, {KEY_ENTER, 13}, {KEY_KP_ENTER, 13},
        {KEY_LEFT_SHIFT, 0x10}, {KEY_RIGHT_SHIFT, 0x10},
        {KEY_LEFT_CONTROL, 0x11}, {KEY_RIGHT_CONTROL, 0x11},
        {KEY_LEFT_ALT, 0x12}, {KEY_RIGHT_ALT, 0x12}, {KEY_PAUSE, 0x13}, {KEY_CAPS_LOCK, 0x14},
        {KEY_SPACE, 0x20}, {KEY_PAGE_UP, 0x21}, {KEY_PAGE_DOWN, 0x22}, {KEY_END, 0x23}, {KEY_HOME, 0x24},
        {KEY_LEFT, 0x25}, {KEY_UP, 0x26}, {KEY_RIGHT, 0x27}, {KEY_DOWN, 0x28},
        {KEY_PRINT_SCREEN, 0x2C}, {KEY_INSERT, 0x2D}, {KEY_DELETE, 0x2E},
        {KEY_KP_MULTIPLY, 0x6A}, {KEY_KP_ADD, 0x6B}, {KEY_KP_SUBTRACT, 0x6D},
        {KEY_KP_DECIMAL, 0x6E}, {KEY_KP_DIVIDE, 0x6F}, {KEY_NUM_LOCK, 0x90}, {KEY_SCROLL_LOCK, 0x91},
        {KEY_SEMICOLON, 0xBA}, {KEY_EQUAL, 0xBB}, {KEY_COMMA, 0xBC}, {KEY_MINUS, 0xBD},
        {KEY_PERIOD, 0xBE}, {KEY_SLASH, 0xBF}, {KEY_GRAVE, 0xC0},
        {KEY_LEFT_BRACKET, 0xDB}, {KEY_BACKSLASH, 0xDC}, {KEY_RIGHT_BRACKET, 0xDD}, {KEY_APOSTROPHE, 0xDE}};
    for (const auto &[physical, encoded] : special) if (physical == key) return encoded;
    return 0;
}
}
Viewport currentViewport() {
    return {1.f, {0.f, 0.f}};
}
FrameInput pollInput(const Viewport &viewport, const ControlBindings &bindings) {
    FrameInput input;
    const auto mouse = GetMousePosition();
    input.mouse = {mouse.x, mouse.y};
    input.insideViewport = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                           IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
                           (input.mouse.x >= viewport.offset.x &&
                           input.mouse.y >= viewport.offset.y &&
                           input.mouse.x < viewport.offset.x + W * viewport.scale &&
                           input.mouse.y < viewport.offset.y + H * viewport.scale);
    if (input.insideViewport) {
        input.mouse = (Vec{mouse.x, mouse.y} - viewport.offset) * (1 / viewport.scale);
        input.leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        input.rightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        input.leftHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        input.rightHeld = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        input.mouseWheel = GetMouseWheelMove();
        input.wheel = input.mouseWheel;
    }
    if (!input.leftHeld && !input.rightHeld) {
        input.leftReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        input.rightReleased = IsMouseButtonReleased(MOUSE_BUTTON_RIGHT);
    }
    if (IsWindowFocused()) {
        input.focused = true;
        static bool composing=false;
        input.entryComposing=nativeImeComposing(GetWindowHandle());
        input.entryCompositionCommitted=composing && !input.entryComposing;
        composing=input.entryComposing;
        input.control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        input.alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        input.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        input.escape = IsKeyPressed(KEY_ESCAPE);
        input.enter = IsKeyPressed(KEY_ENTER);
        input.backspace = IsKeyPressed(KEY_BACKSPACE);
        input.entryHome = IsKeyPressed(KEY_HOME);
        input.entryEnd = IsKeyPressed(KEY_END);
        input.entryDelete = IsKeyPressed(KEY_DELETE);
        input.entryStep = int(IsKeyPressed(KEY_RIGHT)) - int(IsKeyPressed(KEY_LEFT));
        input.entrySelectAll = input.control && IsKeyPressed(KEY_A);
        input.pageDelta = int(IsKeyPressed(KEY_PAGE_DOWN)) - int(IsKeyPressed(KEY_PAGE_UP));
        input.quantityDelta = int(input.wheel) + int(IsKeyPressed(KEY_UP)) - int(IsKeyPressed(KEY_DOWN));
        for (int character = GetCharPressed(); character; character = GetCharPressed()) {
            if(character>=32) input.entryUnicode+=char32_t(character);
            if (character >= 32 && character < 127) {
                input.entryText += char(character);
                if (character >= '0' && character <= '9') input.text += char(character);
            } else if (character >= 127) input.entryUnsupported = true;
        }
        input.tab = IsKeyPressed(KEY_TAB);
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
        // Native automap callbacks consume key-down events, including the OS
        // repeat, and move 16 map pixels per event (not per rendered frame).
        const auto mapKey = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
        input.movement = {float(int(mapKey(KEY_RIGHT)) - int(mapKey(KEY_LEFT))),
                          float(int(mapKey(KEY_DOWN)) - int(mapKey(KEY_UP)))};
        std::array<bool, controlWheelDown + 1> pressed{}, held{};
        for (int physical = KEY_SPACE; physical <= KEY_KB_MENU; ++physical) {
            const auto key = controlKey(physical);
            if (!key) continue;
            pressed[key] = pressed[key] || IsKeyPressed(physical);
            held[key] = held[key] || IsKeyDown(physical);
            if (!input.controlKeyPressed && IsKeyPressed(physical)) input.controlKeyPressed = key;
        }
        for (const auto &[button, key] : {std::pair{MOUSE_BUTTON_MIDDLE, 4},
                std::pair{MOUSE_BUTTON_SIDE, 5}, std::pair{MOUSE_BUTTON_EXTRA, 6}}) {
            pressed[size_t(key)] = input.insideViewport && IsMouseButtonPressed(button);
            held[size_t(key)] = input.insideViewport && IsMouseButtonDown(button);
            if (!input.controlKeyPressed && pressed[size_t(key)]) input.controlKeyPressed = uint16_t(key);
        }
        pressed[controlWheelUp] = input.wheel > 0;
        pressed[controlWheelDown] = input.wheel < 0;
        if (!input.controlKeyPressed && input.wheel) input.controlKeyPressed = input.wheel > 0 ? controlWheelUp : controlWheelDown;
        const bool debugChord = input.save || input.reload || input.debugNpc || input.debugWaypoint || input.debugLoadout ||
            input.debugHitpoints || input.debugMenu || input.debugSkillTree || input.debugExperience || input.debugMap ||
            input.debugCombat || input.debugSkills || input.debugCharacter || input.debugItem || input.debugPet ||
            input.debugOnline || input.debugHost || input.debugQuit || input.capture ||
            (input.control && IsKeyPressed(KEY_Z));
        const auto active = [&](ControlAction action, bool down = false) {
            if (!down && debugChord) return false;
            for (const auto key : bindings[action]) {
                if (!key || key >= pressed.size()) continue;
                if (down ? held[key] : pressed[key]) return true;
            }
            return false;
        };
        input.inventory = active(ControlAction::Inventory);
        input.character = active(ControlAction::Character);
        input.skillTree = active(ControlAction::SkillTree);
        input.skillPicker = active(ControlAction::SkillPicker);
        input.quests = active(ControlAction::QuestLog);
        input.hireling = active(ControlAction::Hireling);
        input.party = active(ControlAction::Party);
        input.messageLog = active(ControlAction::MessageLog);
        input.run = active(ControlAction::RunLock);
        for(size_t slot=0;slot<8;++slot)
            if(active(ControlAction(size_t(ControlAction::Say0)+slot))) input.voice=int(slot);
        input.holdRun = active(ControlAction::Run, true);
        input.standStill = active(ControlAction::StandStill, true);
        input.expandBelt = active(ControlAction::Belt);
        input.weaponSwap = active(ControlAction::SwapWeapons);
        input.minimapSide = active(ControlAction::Minimap);
        input.automap = active(ControlAction::Automap);
        input.automapCenter = active(ControlAction::AutomapCenter);
        input.automapNames = active(ControlAction::AutomapNames);
        input.automapParty = active(ControlAction::AutomapParty);
        input.automapFade = active(ControlAction::AutomapFade);
        input.screenshot = active(ControlAction::Screenshot);
        input.help = active(ControlAction::Help);
        input.chat = active(ControlAction::Chat);
        input.togglePortraits = active(ControlAction::Portraits);
        input.clearScreen = active(ControlAction::ClearScreen);
        input.clearMessages = active(ControlAction::ClearMessages);
        input.previousSkill = active(ControlAction::PreviousSkill);
        input.nextSkill = active(ControlAction::NextSkill);
        input.bhSettings = input.control && IsKeyPressed(KEY_Z);
        input.showLoot = active(ControlAction::ShowItems, true);
        for (size_t slot = 0; slot < input.belt.size(); ++slot)
            input.belt[slot] = active(ControlAction(size_t(ControlAction::Belt1) + slot));
        for (size_t slot = 0; slot < input.skills.size(); ++slot)
            input.skills[slot] = active(ControlAction(size_t(ControlAction::Skill1) + slot));
    } else {
        input.focused = false;
        input.insideViewport = false;
        input.leftHeld = input.rightHeld = false;
    }
    return input;
}
} // namespace d2x

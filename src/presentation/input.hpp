#pragma once
#include "core/math.hpp"
#include "gameplay/model/definitions.hpp"
#include <array>
#include <string>

namespace d2x {
inline constexpr size_t hotbarSlots = 8;
// Device-independent snapshot. Only app/input.cpp knows physical key codes.
struct FrameInput {
    Vec mouse, movement;
    bool insideViewport = false;
    bool leftPressed = false, leftHeld = false, leftReleased = false, rightHeld = false, rightPressed = false;
    bool inventory = false, character = false, skillTree = false, quests = false, shift = false, control = false, enter = false, focused = true;
    int quantityDelta = 0, pageDelta = 0, menuDelta = 0;
    std::string text;
    std::string entryText;
    bool tab = false;
    float wheel = 0;
    bool backspace = false;
    bool showLoot = false;
    bool help = false, automap = false, collision = false;
    bool minimapSide = false;
    bool automapCenter = false;
    bool automapNames = false;
    bool hireling = false;
    bool run = false, escape = false, screenshot = false;
    bool expandBelt = false, storage = false, weaponSwap = false;
    bool save = false, load = false;
    bool debugGold = false, debugExperience = false;
    bool debugCube = false;
    bool debugAttributes = false, debugTalents = false;
    bool debugWaypoints = false;
    std::array<bool, 4> belt{};
    std::array<bool, hotbarSlots> skills{};
};
} // namespace d2x

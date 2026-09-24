#pragma once
#include "core/math.hpp"
#include "gameplay/model/definitions.hpp"
#include <array>

namespace d2x {
inline constexpr size_t hotbarSlots = 8;
// Device-independent snapshot. Only app/input.cpp knows physical key codes.
struct FrameInput {
    Vec mouse, movement;
    bool insideViewport = false;
    bool leftPressed = false, leftHeld = false, leftReleased = false, rightHeld = false, rightPressed = false;
    bool inventory = false, character = false, skillTree = false, shift = false, control = false, enter = false, focused = true;
    int quantityDelta = 0, pageDelta = 0;
    bool showLoot = false;
    bool help = false, automap = false, travel = false, collision = false;
    bool pause = false, mute = false, run = false, restart = false, escape = false, screenshot = false;
    bool expandBelt = false, storage = false;
    bool save = false, load = false;
    bool debugGold = false, debugExperience = false;
    bool debugAttributes = false, debugTalents = false, debugCharacter = false;
    bool debugWaypoints = false;
    std::array<bool, 4> belt{};
    std::array<bool, hotbarSlots> skills{};
};
} // namespace d2x

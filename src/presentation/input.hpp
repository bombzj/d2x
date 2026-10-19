#pragma once
#include "core/math.hpp"
#include <cstddef>
#include <array>
#include <string>

namespace d2x {
inline constexpr size_t hotbarSlots = 8;
// Device-independent snapshot. Only app/input.cpp knows physical key codes.
struct FrameInput {
    Vec mouse, movement; // movement: automap directional key events, including repeat
    bool insideViewport = false;
    bool leftPressed = false, leftHeld = false, leftReleased = false, rightHeld = false, rightPressed = false, rightReleased = false;
    bool inventory = false, character = false, skillTree = false, quests = false, shift = false, control = false, enter = false, focused = true;
    int quantityDelta = 0, pageDelta = 0, menuDelta = 0;
    std::string text;
    std::string entryText;
    bool tab = false, chat = false, clear = false, end = false;
    float wheel = 0, mouseWheel = 0;
    int menuUp = 0, menuDown = 0, menuLeft = 0, menuRight = 0, menuSelect = 0, menuCancel = 0;
    bool alt = false;
    bool backspace = false;
    bool messageLog = false, entryHome = false, entryEnd = false, entryDelete = false, entryUnsupported = false;
    int entryStep = 0;
    bool entrySelectAll = false;
    bool showLoot = false;
    bool help = false, automap = false, collision = false;
    bool minimapSide = false;
    bool automapCenter = false;
    bool automapNames = false;
    bool hireling = false, party = false, togglePortraits = false;
    bool run = false, escape = false, screenshot = false, pause = false;
    bool debugNpc = false, debugWaypoint = false, debugLoadout = false, debugHitpoints = false, debugMenu = false, debugSkillTree = false, debugExperience = false, debugMap = false, debugCombat = false, debugSkills = false, debugCharacter = false, debugItem = false, debugPet = false, debugOnline = false, debugHost = false, debugQuit = false, capture = false;
    bool save = false, load = false, reload = false;
    bool expandBelt = false, weaponSwap = false;
    std::array<bool, 4> belt{};
    std::array<bool, hotbarSlots> skills{};
};
} // namespace d2x

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2x {
enum class ControlAction : size_t {
    Character, Inventory, Party, Hireling, MessageLog, QuestLog, Help,
    SkillTree, SkillPicker,
    Skill1, Skill2, Skill3, Skill4, Skill5, Skill6, Skill7, Skill8,
    Skill9, Skill10, Skill11, Skill12, Skill13, Skill14, Skill15, Skill16,
    PreviousSkill, NextSkill, Belt, Belt1, Belt2, Belt3, Belt4, SwapWeapons,
    Chat, Run, RunLock, StandStill, ShowItems, Portraits,
    Automap, AutomapCenter, AutomapFade, AutomapParty, AutomapNames, Minimap,
    Say0, Say1, Say2, Say3, Say4, Say5, Say6, Say7,
    Screenshot, ClearScreen, ClearMessages, Count
};
inline constexpr uint16_t controlWheelUp = 0x100, controlWheelDown = 0x101;
struct ControlBindings {
    using Keys = std::array<uint16_t, 2>;
    std::array<Keys, size_t(ControlAction::Count)> keys{};
    const Keys &operator[](ControlAction action) const { return keys[size_t(action)]; }
    Keys &operator[](ControlAction action) { return keys[size_t(action)]; }
    bool operator==(const ControlBindings &) const = default;
};
ControlBindings defaultControlBindings();
bool validControlKey(uint16_t key);
bool validControlBinding(ControlAction, uint16_t key);
bool controlActionAvailable(ControlAction);
void assignControlKey(ControlBindings &, ControlAction, size_t column, uint16_t key);
} // namespace d2x
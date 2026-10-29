#include "control_bindings.hpp"

namespace d2x {
ControlBindings defaultControlBindings() {
    ControlBindings result;
    result[ControlAction::Character] = {'A', 'C'};
    result[ControlAction::Inventory] = {'I', 'B'};
    result[ControlAction::Party] = {'P', 0};
    result[ControlAction::Hireling] = {'O', 0};
    result[ControlAction::MessageLog] = {'M', 0};
    result[ControlAction::QuestLog] = {'Q', 0};
    result[ControlAction::Help] = {'H', 0};
    result[ControlAction::SkillTree] = {'T', 0};
    result[ControlAction::SkillPicker] = {'S', 0};
    for (size_t slot = 0; slot < 8; ++slot)
        result.keys[size_t(ControlAction::Skill1) + slot] = {uint16_t(0x70 + slot), 0};
    result[ControlAction::PreviousSkill] = {controlWheelUp, 0};
    result[ControlAction::NextSkill] = {controlWheelDown, 0};
    result[ControlAction::Belt] = {0xC0, 0};
    for (size_t slot = 0; slot < 4; ++slot)
        result.keys[size_t(ControlAction::Belt1) + slot] = {uint16_t('1' + slot), 0};
    result[ControlAction::SwapWeapons] = {'W', 0};
    result[ControlAction::Chat] = {0x0D, 0};
    result[ControlAction::RunLock] = {'R', 0};
    result[ControlAction::StandStill] = {0x10, 0};
    result[ControlAction::ShowItems] = {0x12, 0};
    result[ControlAction::Portraits] = {'Z', 0};
    result[ControlAction::Automap] = {0x09, 0};
    result[ControlAction::AutomapCenter] = {0x79, 0};
    result[ControlAction::AutomapFade] = {0x78, 0};
    result[ControlAction::AutomapParty] = {0x7A, 0};
    result[ControlAction::AutomapNames] = {0x7B, 0};
    result[ControlAction::Minimap] = {'V', 0};
    for (size_t say = 0; say < 8; ++say)
        result.keys[size_t(ControlAction::Say0) + say] = {uint16_t(0x60 + say), 0};
    result[ControlAction::Screenshot] = {0x2C, 0};
    result[ControlAction::ClearScreen] = {0x20, 0};
    result[ControlAction::ClearMessages] = {'N', 0};
    return result;
}
bool validControlKey(uint16_t key) {
    return key == 0 || key == 4 || key == 5 || key == 6 || key == 8 || key == 9 || key == 13 ||
        (key >= 0x10 && key <= 0x14) || (key >= 0x20 && key <= 0x28) ||
        (key >= 0x2C && key <= 0x2E) || (key >= '0' && key <= '9') ||
        (key >= 'A' && key <= 'Z') || (key >= 0x60 && key <= 0x6F && key != 0x6C) ||
        (key >= 0x70 && key <= 0x7B) || key == 0x90 || key == 0x91 ||
        (key >= 0xBA && key <= 0xC0) || (key >= 0xDB && key <= 0xDE) ||
        key == controlWheelUp || key == controlWheelDown;
}
bool controlActionAvailable(ControlAction action) {
    return action < ControlAction::Count;
}
bool validControlBinding(ControlAction action, uint16_t key) {
    if (action >= ControlAction::Count || !validControlKey(key)) return false;
    const bool held = action == ControlAction::Run || action == ControlAction::StandStill || action == ControlAction::ShowItems;
    return !held || (key != controlWheelUp && key != controlWheelDown);
}
void assignControlKey(ControlBindings &bindings, ControlAction action, size_t column, uint16_t key) {
    if (column >= 2 || !validControlBinding(action, key)) return;
    if (key)
        for (auto &pair : bindings.keys)
            for (auto &current : pair)
                if (current == key) current = 0;
    bindings[action][column] = key;
}
} // namespace d2x

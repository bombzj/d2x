#pragma once

namespace d2x {
struct SkillHotkey {
    int skill = -2; // -2 unbound, -1 ordinary attack, otherwise MPQ skill ID.
    bool right = true;
};
} // namespace d2x

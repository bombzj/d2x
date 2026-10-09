#pragma once
#include <cstdint>

namespace d2x {
struct SkillHotkey {
    int skill = -2; // -2 unbound, -1 ordinary attack, otherwise MPQ skill ID.
    bool right = true;
    uint32_t owner = UINT32_MAX; // Runtime item source; D2S rebinds after import.
};
} // namespace d2x

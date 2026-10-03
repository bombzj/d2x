#pragma once
#include <array>
#include <cstdint>

namespace d2x {
using EffectFrame = uint64_t; // Absolute 25 Hz simulation frame; never wall-clock time.
enum class CurseAi { None, DimVision, Terror, Confuse, Attract };
// Imported States.txt fields. These describe a state, not a particular skill.
struct CombatStateDefinition {
    int id = -1, group = 0;
    bool removeOnHit = false;
    std::array<bool, 3> stayOnDeath{};
    bool staminaBarBlue = false;
    bool curse = false;
    bool hideOnDeath = false, shatterOnDeath = false, corpseUnselectable = false;
    bool curable = false;
};
} // namespace d2x

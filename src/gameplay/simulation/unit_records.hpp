#pragma once
#include "gameplay/combat/unit.hpp"

namespace d2x {
struct PlayerState;
struct HirelingState;
struct Enemy;
// Internal authority bindings, never exposed through a skill or client port.
struct UnitRecordBindings {
    PlayerState *player = nullptr;
    HirelingState *hireling = nullptr;
    Enemy *monster = nullptr;
};
struct RuntimeCombatUnit : CombatUnit {
    UnitRecordBindings records;
};
} // namespace d2x

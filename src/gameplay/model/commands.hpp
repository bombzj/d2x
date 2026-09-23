#pragma once
#include "core/id.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/model/definitions.hpp"
#include <variant>

namespace d2x {
struct MoveTo {
    Vec position;
};
struct Attack {
    EntityId target;
};
struct DebugKill {
    EntityId target;
};
struct CastSkill {
    Skill skill;
    Vec target;
};
struct ToggleRun {};
struct StopMoving {};
struct Interact {
    EntityId target;
};
struct Travel {
    RegionId destination;
};
struct WaypointTravel {
    EntityId source;
    RegionId destination;
};
struct UseExit {
    int slot = 0;
};
struct RestartArea {};
struct CloseStorage {};
struct UseTownPortal {
    uint64_t revision;
};
struct PickupItem {
    ItemHandle item;
};
// UI supplies intentions; only the gameplay layer changes authoritative state.
using GameCommand =
    std::variant<MoveTo, Attack, CastSkill, ToggleRun, Interact, Travel, RestartArea, MoveItem, SwapItems,
                 SplitStack, MergeStacks, PickupItem, StopMoving, EquipBelt, UseItem, UseBeltColumn,
                 CloseStorage, TransferItem, UseExit, EquipItem, DebugKill, UseTownPortal, WaypointTravel>;
} // namespace d2x

#pragma once
#include "server/runtime/contracts.hpp"

namespace d2x::server::travel {
// Travel/waypoint/portal transition preparation; destination content arrives through World.
enum class Kind { Exit, Waypoint, Portal, Npc, SpecialPortal, QuestObject, NativeObjectPortal, RefreshUnit, ClearBusy };
struct Request { Kind kind; UnitTarget source; std::optional<RegionId> destination; };
}

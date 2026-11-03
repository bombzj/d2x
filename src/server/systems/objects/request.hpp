#pragma once
#include "server/runtime/contracts.hpp"

namespace d2x::server::objects {
// Runtime doors/chests/shrines/wells/quest objects; resource-only map decorations are separate.
enum class Action { Operate, HighlightDoor };
struct Request { UnitTarget target; Action action=Action::Operate; };
}

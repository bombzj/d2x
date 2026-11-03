#pragma once
#include "server/runtime/contracts.hpp"

namespace d2x::server::skills {
// Skill selection and cast/channel lifecycle; formulas use prepared pure skill definitions.
enum class Action { Select, Cast, Stop, Bind };
struct Request { Action action; uint16_t skill{}; bool right{}, repeat{}, stationary{}; std::optional<ActionTarget> target; std::optional<unsigned> hotkey; uint32_t owner=UINT32_MAX; };
}

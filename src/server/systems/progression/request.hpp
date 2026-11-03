#pragma once
#include "gameplay/character/intents.hpp"
#include <variant>

namespace d2x::server::progression {
// Experience, level, allocated stats/skills and quest rewards; no client-side grants.
using Intent = std::variant<AllocateAttribute, AllocateSkill>;
struct Request { Intent intent; unsigned count = 1; };
}

#pragma once
#include "server/runtime/contracts.hpp"

namespace d2x::server::death {
// Death occurrences, resurrection and corpse recovery; no duplicated persistent corpse items.
enum class Action { Resurrect, RecoverCorpse };
struct Request { Action action; std::optional<UnitTarget> corpse; };
}

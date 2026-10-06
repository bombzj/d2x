#pragma once
#include "curse_spec.hpp"

namespace d2x {
// Pure display formula. Damage and target effects remain server-owned.
CurseSpec evaluateCurse(const CurseSpec &spec, int rank);
} // namespace d2x

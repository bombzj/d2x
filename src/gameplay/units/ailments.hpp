#pragma once
#include "core/id.hpp"

namespace d2x {
// Borrowed authority capabilities; the original unit record owns these values.
struct PeriodicDamageView {
    float *remaining = nullptr, *rate = nullptr;
    EntityId *source = nullptr;
};
struct WebSlowView {
    float *remaining = nullptr;
    int *percent = nullptr;
    EntityId *source = nullptr;
};
} // namespace d2x

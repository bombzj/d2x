#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <cstddef>
#include <cstdint>

namespace d2x::server {
// Compact bounded observation, independent of reliable delivery and acknowledgement.
struct DiagnosticEvent {
    uint64_t sequence{}, batch{}, tick{}, transaction{};
    size_t type{};
    EntityId actor, target;
    RegionId area{};
    int64_t value{}, secondary{};
    Vec position;
};
}

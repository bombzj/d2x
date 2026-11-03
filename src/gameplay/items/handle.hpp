#pragma once
#include "core/entity_id.hpp"
#include <cstdint>

namespace d2x {
struct ItemHandle {
    EntityId id;
    uint64_t revision = 0;
};
} // namespace d2x

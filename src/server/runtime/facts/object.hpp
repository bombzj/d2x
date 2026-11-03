#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <cstdint>
#include <optional>

namespace d2x::server {
// Frozen at the authoritative frame boundary, before later ENDANIM callbacks.
// Initial visibility uses a current object snapshot instead of replaying this.
struct ObjectFact {
    EntityId object, actor; RegionId area; uint64_t revision{};
    Vec position; int mode{}; uint8_t selectionFlags{}; std::optional<int> shrine{};
};
}

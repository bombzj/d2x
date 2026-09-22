#pragma once
#include "core/id.hpp"

namespace d2x {
// Transient, server-authoritative access to an external container. Inventory
// ownership persists after this grant is closed or invalidated by world state.
struct StorageAccess {
    EntityId object, container;
    explicit operator bool() const { return bool(object) && bool(container); }
};
} // namespace d2x

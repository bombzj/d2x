#pragma once
#include <compare>
#include <cstdint>

namespace d2x {
struct EntityId {
    uint64_t value = 0;
    explicit operator bool() const { return value != 0; }
    auto operator<=>(const EntityId &) const = default;
};
}
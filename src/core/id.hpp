#pragma once
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
// IDs are session-wide, never vector indices. Zero always means no entity.
struct EntityId {
    uint64_t value = 0;
    explicit operator bool() const { return value != 0; }
    auto operator<=>(const EntityId &) const = default;
};
class EntityIds {
    uint64_t next_ = 1;

  public:
    explicit EntityIds(uint64_t next = 1) : next_(next) {
        if (!next_) throw std::invalid_argument("Entity ID cursor cannot be zero");
    }
    uint64_t cursor() const { return next_; }
    EntityId allocate() {
        if (next_ == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Entity ID space exhausted");
        return {next_++};
    }
};
} // namespace d2x

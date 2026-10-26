#pragma once
#include "core/id.hpp"
#include <cstdint>
#include <optional>
namespace d2x {
struct OpenWoundsApplication {
    int64_t rate{};EntityId source,credit;uint64_t until{},next{};unsigned pd2Stacks{};
    // One contact's half-rate, kept apart from the recipient's accumulated
    // stacks so Rathma's partner can retain its own list/source/history.
    std::optional<int64_t> pd2Increment;
    EntityId pd2Attacker,pd2Credit;
};
}

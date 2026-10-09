#pragma once
#include <array>
#include <cstdint>
#include <compare>
#include <optional>
#include <string>

namespace d2x {
// Original IFLAG_ISEAR payload; separate from the base item's level/quality.
struct ItemEarIdentity {
    unsigned characterClass = 0, level = 0;
    std::string name;
    auto operator<=>(const ItemEarIdentity &) const = default;
};
// Native v96 disk provenance, including the reserved third DWORD. Never sent
// over GS or interpreted as authority/account ownership.
using ItemRealmIdentity = std::array<uint32_t, 3>;
} // namespace d2x

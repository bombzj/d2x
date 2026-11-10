#pragma once
#include "core/bytes.hpp"
#include <optional>
#include <span>

namespace d2x {
struct ClassicData;
struct CharacterSaveFiles {
    Bytes main;
    std::optional<Bytes> extended;
    bool operator==(const CharacterSaveFiles &) const = default;
};

// Keep the disk layout at the persistence boundary. The shared character
// decoder and the TCP/IP wire still consume a single native D2S byte stream.
Bytes combinePd2Save(const CharacterSaveFiles &, const ClassicData &);
CharacterSaveFiles splitPd2Save(std::span<const uint8_t> complete,
    std::span<const uint8_t> extendedLayout, const ClassicData &);
} // namespace d2x

#pragma once
#include "core/bytes.hpp"
#include <span>
#include <array>
#include <optional>
namespace d2x {
struct ClassicData;
// Native PD2 header/extension rules; old private D2X prototypes are rejected.
void rejectPrivatePd2Save(std::span<const uint8_t>);
void preparePd2SaveHeader(const ClassicData &, uint8_t &skillCount);
void validatePd2SaveExtension(std::span<const uint8_t>);
std::optional<std::array<uint16_t,17>> readPd2SavedCounters(std::span<const uint8_t>);
Bytes writePd2SavedCounters(std::span<const uint8_t>,const std::optional<std::array<uint16_t,17>> &);
}

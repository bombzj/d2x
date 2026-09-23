#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace d2x {
// Decode original MPQ text members, including the UTF-16 dialogue documents.
std::string decodeText(std::span<const uint8_t> bytes);
} // namespace d2x

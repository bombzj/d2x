#pragma once
#include "core/bytes.hpp"
#include "resources/data_table.hpp"
#include <cstdint>
#include <map>
#include <span>

namespace d2x {
struct D2sSkills {
    std::map<int, uint8_t> ranks;
    size_t bytesRead = 0;
};
D2sSkills readD2sSkills(std::span<const uint8_t> bytes, uint8_t characterClass,
                       uint8_t skillCount, const DataTable &skills);
void writeD2sSkills(Bytes &bytes, uint8_t characterClass, uint8_t skillCount,
                    const std::map<int, uint8_t> &ranks, const DataTable &skills);
} // namespace d2x
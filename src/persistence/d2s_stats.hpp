#pragma once
#include "core/bytes.hpp"
#include "resources/data_table.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace d2x {
struct D2sStat {
    uint16_t id = 0;
    int64_t value = 0;
    int32_t parameter = 0;
};
struct D2sStats {
    std::vector<D2sStat> values;
    size_t bytesRead = 0;
};
D2sStats readD2sStats(std::span<const uint8_t> bytes, const DataTable &itemStatCost);
void writeD2sStats(Bytes &bytes, const std::vector<D2sStat> &values, const DataTable &itemStatCost);
} // namespace d2x
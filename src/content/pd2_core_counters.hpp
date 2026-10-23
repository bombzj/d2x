#pragma once
#include "core/bytes.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace d2x {
class DataTable;
std::pair<std::vector<int16_t>, std::vector<int16_t>> preparePd2CoreCounterSlots(
    const DataTable &misc, const Bytes &compiled);
}
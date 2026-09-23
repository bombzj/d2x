#pragma once
#include "resources/archive.hpp"
#include <array>
#include <map>
#include <string>
#include <string_view>

namespace d2x {
struct AnimDataRecord {
    uint32_t frames = 0;
    int32_t speed = 0;
    std::array<uint8_t, 144> frameFlags{};
};
class AnimDataTable {
    std::map<std::string, AnimDataRecord, std::less<>> records_;

  public:
    explicit AnimDataTable(const Bytes &data);
    const AnimDataRecord *find(std::string_view key) const;
};
} // namespace d2x

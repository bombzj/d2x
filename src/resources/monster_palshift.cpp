#include "monster_palshift.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
std::array<uint8_t, 256> monsterPalshift(const Bytes &data, int transLevel) {
    // MonStats.TransLvl is relative to the third map (zero-based index 2).
    // See OpenDiablo2 monster_stats_record.go, PaletteId. Adding 3 shifts every
    // subtype to its neighbour, including the colors baked into DT/DD frames.
    constexpr int firstMonsterMap = 2;
    if (data.empty() || data.size() % 256 != 0 || transLevel < 0 ||
        size_t(firstMonsterMap + transLevel + 1) > data.size() / 256)
        throw std::runtime_error("Invalid MPQ monster palshift.dat or TransLvl");
    std::array<uint8_t, 256> result;
    const auto offset = size_t(firstMonsterMap + transLevel) * result.size();
    std::copy_n(data.begin() + offset, result.size(), result.begin());
    if (result[0] != 0)
        throw std::runtime_error("Monster palshift.dat changes transparent index");
    return result;
}
} // namespace d2x

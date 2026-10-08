#include "components.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
MonsterComponentPalette monsterComponentPalette(const MonsterComponents &counts, uint64_t &random) {
    std::vector<size_t> varying;
    unsigned sum = 0;
    for (auto count : counts) if (count > 1) sum += count - 1;
    // D2Common's BYTE accumulator saturates at 254; the native x86 shift
    // masks its count to five bits. A signed-negative maximum adds no variant.
    const unsigned shift = std::min(sum, 254u) & 31u;
    if (shift == 31) return {};
    const unsigned capacity = std::min(1u << shift, 3u);
    MonsterComponents base{};
    for (size_t c = 0; c < counts.size(); ++c) if (counts[c] > 1) {
        varying.push_back(c);
        base[c] = uint8_t(limitedRandom(random, counts[c]));
    }
    MonsterComponentPalette result{base};
    if (capacity == 1 || varying.empty()) return result;
    auto first = varying.front(), second = first;
    if (varying.size() > 1) {
        const size_t firstIndex = limitedRandom(random, uint32_t(varying.size()));
        first = varying[firstIndex];
        varying[firstIndex] = varying.back();
        second = varying[limitedRandom(random, uint32_t(varying.size() - 1))];
    }
    while (result.size() < capacity) {
        auto candidate = base;
        int retries = 3;
        bool duplicate;
        do {
            candidate[first] = uint8_t(limitedRandom(random, counts[first]));
            if (second != first) candidate[second] = uint8_t(limitedRandom(random, counts[second]));
            duplicate = false;
            for (const auto &previous : result) if (previous == candidate) {
                duplicate = true;
                --retries;
            }
        } while (duplicate && retries != 0);
        // Original retains the last candidate after retry exhaustion.
        result.push_back(candidate);
    }
    return result;
}
MonsterComponents chooseMonsterComponents(const MonsterComponents &counts,
    const MonsterComponentPalette &palette, uint64_t &random) {
    if (!palette.empty()) return palette[limitedRandom(random, uint32_t(palette.size()))];
    MonsterComponents result{};
    for (size_t c = 0; c < 12; ++c) result[c] = uint8_t(limitedRandom(random, counts[c]));
    return result;
}
}

#include "components.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
int32_t rollMonsterLife(int minimum,int maximum,uint64_t &random) {
    return nativeAdd(minimum,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(maximum,minimum),1)))));
}
MonsterComponents chooseRegionMonsterComponents(const MonsterComponents &counts,int nativeClass,int totalPieces,
    std::map<int,MonsterComponentPalette> &palettes,uint64_t &random) {
    auto palette=palettes.find(nativeClass);
    if(palette==palettes.end() && totalPieces>2 && palettes.size()<13)
        palette=palettes.emplace(nativeClass,monsterComponentPalette(counts,random)).first;
    return chooseMonsterComponents(counts,palette==palettes.end()?MonsterComponentPalette{}:palette->second,random);
}

void applyNecroSkeletonComponents(MonsterComponents &components,int level,bool shield,std::optional<uint8_t> element) {
    constexpr std::array<std::array<uint8_t,9>,11> variants{{
        {0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0},
        {0,1,0,0,0,0,0,0,1},{0,1,0,0,0,0,0,1,1},
        {1,1,0,0,0,0,1,1,2},{1,1,0,0,0,0,1,1,2},
        {1,1,1,0,0,1,1,1,3},{2,2,1,0,0,1,1,1,3},
        {2,2,1,0,0,1,1,2,4},{2,3,1,0,0,1,2,2,4},
        {2,3,1,0,0,1,2,2,4}}};
    if(level<0) throw std::invalid_argument("Negative necromancer component level");
    const auto &variant=variants.at(size_t(std::min(level,10)));
    constexpr std::array<size_t,7> slots{0,1,8,9,2,3,4};
    constexpr std::array<size_t,7> fields{0,2,3,4,5,6,7};
    for(size_t index=0;index<slots.size();++index) components[slots[index]]=variant[fields[index]];
    if(shield) components[7]=variant[1];
    if(element) components[11]=components[12]=*element;
    else components[5]=variant[8];
}
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

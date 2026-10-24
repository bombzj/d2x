#pragma once
#include <algorithm>
#include <array>

namespace d2x {
inline bool pd2SearchableCorpseClass(int nativeClass) {
    // Current core 17d5fb1b, 0x36FAF0: union of the six native boss
    // identity sets initialized at 0x191110..0x1914F0. These are code
    // exclusions, not the MonStats boss flag (ordinary bosses differ).
    constexpr std::array excluded{743,746,747,750,755,800,809,826,861,870,
        879,882,883,884,893,894,915,963,964,996,997,998,1000,1025,1026,
        1058,1060,1061,1062,1063,1094,1096,1103,1104,1105,1121,1122,
        1148,1149,1164,1166,1174,1175,1240,1242,1243};
    return !std::binary_search(excluded.begin(),excluded.end(),nativeClass);
}
}

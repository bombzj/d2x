#pragma once
#include <array>
#include <cstddef>

namespace d2x {
struct ClientMissileCapabilities {
    int function;
    bool stationary;
    bool pairCreationSources;
    bool allowChildArtwork;
};
inline constexpr std::array clientMissileCapabilities{
    ClientMissileCapabilities{1, false, false, false},
    ClientMissileCapabilities{2, false, false, false},
    ClientMissileCapabilities{3, false, false, false},
    ClientMissileCapabilities{4, false, false, false},
    ClientMissileCapabilities{5, true, true, false},
    ClientMissileCapabilities{6, false, true, false},
    ClientMissileCapabilities{7, false, false, false},
    ClientMissileCapabilities{8, false, false, true},
    ClientMissileCapabilities{9, true, true, false},
    ClientMissileCapabilities{11, true, true, false},
    ClientMissileCapabilities{13, true, true, true},
    ClientMissileCapabilities{17, true, false, false},
    ClientMissileCapabilities{18, false, false, true},
    ClientMissileCapabilities{19, false, false, false},
    ClientMissileCapabilities{20, false, false, false},
    ClientMissileCapabilities{21, true, false, true},
    ClientMissileCapabilities{22, false, false, true},
    ClientMissileCapabilities{27, false, false, false},
    ClientMissileCapabilities{29, true, false, true},
    ClientMissileCapabilities{47, false, true, true},
    ClientMissileCapabilities{48, true, true, true},
    ClientMissileCapabilities{49, false, false, true},
    ClientMissileCapabilities{50, true, true, true},
    ClientMissileCapabilities{51, true, false, true},
    ClientMissileCapabilities{52, false, true, true},
    ClientMissileCapabilities{55, false, false, true},
    ClientMissileCapabilities{56, true, false, false},
    ClientMissileCapabilities{58, false, false, false},
    ClientMissileCapabilities{60, false, false, false},
};
constexpr const ClientMissileCapabilities *findClientMissileCapabilities(int function) {
    for (const auto &program : clientMissileCapabilities)
        if (program.function == function) return &program;
    return nullptr;
}
static_assert([] {
    for (std::size_t index = 1; index < clientMissileCapabilities.size(); ++index)
        if (clientMissileCapabilities[index - 1].function >= clientMissileCapabilities[index].function) return false;
    return true;
}());
}

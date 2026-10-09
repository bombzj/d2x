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
    ClientMissileCapabilities{3, false, false, false},
    ClientMissileCapabilities{4, false, false, false},
    ClientMissileCapabilities{5, true, true, false},
    ClientMissileCapabilities{6, false, true, false},
    ClientMissileCapabilities{7, false, false, false},
    ClientMissileCapabilities{8, false, false, true},
    ClientMissileCapabilities{9, true, true, false},
    ClientMissileCapabilities{13, true, true, true},
    ClientMissileCapabilities{18, false, false, true},
    ClientMissileCapabilities{19, false, false, false},
    ClientMissileCapabilities{20, false, false, false},
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
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
    ClientMissileCapabilities{10, true, false, true},
    ClientMissileCapabilities{11, true, true, false},
    ClientMissileCapabilities{12, false, false, false},
    ClientMissileCapabilities{13, true, true, true},
    ClientMissileCapabilities{15, false, false, true},
    ClientMissileCapabilities{16, false, false, true},
    ClientMissileCapabilities{17, true, false, false},
    ClientMissileCapabilities{18, false, false, true},
    ClientMissileCapabilities{19, false, false, false},
    ClientMissileCapabilities{20, false, false, false},
    ClientMissileCapabilities{21, true, false, true},
    ClientMissileCapabilities{22, false, false, true},
    ClientMissileCapabilities{23, false, false, false},
    ClientMissileCapabilities{25, false, false, true},
    ClientMissileCapabilities{27, false, false, false},
    ClientMissileCapabilities{29, true, false, true},
    ClientMissileCapabilities{30, false, false, true},
    ClientMissileCapabilities{32, false, false, false},
    ClientMissileCapabilities{37, false, false, false},
    ClientMissileCapabilities{38, false, false, false},
    ClientMissileCapabilities{39, false, false, false},
    ClientMissileCapabilities{40, false, false, false},
    ClientMissileCapabilities{41, false, false, false},
    ClientMissileCapabilities{43, true, true, false},
    ClientMissileCapabilities{45, false, false, true},
    ClientMissileCapabilities{46, false, true, true},
    ClientMissileCapabilities{47, false, true, true},
    ClientMissileCapabilities{48, true, true, true},
    ClientMissileCapabilities{49, false, false, true},
    ClientMissileCapabilities{50, true, true, true},
    ClientMissileCapabilities{51, true, false, true},
    ClientMissileCapabilities{52, false, true, true},
    ClientMissileCapabilities{54, false, false, false},
    ClientMissileCapabilities{55, false, false, true},
    ClientMissileCapabilities{56, true, false, false},
    ClientMissileCapabilities{57, false, false, true},
    ClientMissileCapabilities{58, false, false, false},
    ClientMissileCapabilities{59, false, false, false},
    ClientMissileCapabilities{60, false, false, false},
    ClientMissileCapabilities{62, false, false, false},
    ClientMissileCapabilities{63, false, false, false},
    ClientMissileCapabilities{68, false, false, true},
    ClientMissileCapabilities{69, false, true, true},
    ClientMissileCapabilities{70, false, false, false},
    ClientMissileCapabilities{71, false, false, true},
    ClientMissileCapabilities{72, false, false, false},
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

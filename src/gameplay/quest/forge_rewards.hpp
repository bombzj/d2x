#pragma once
#include <array>
#include <string>

namespace d2x {
// A4Q3's fixed program indexes identities validated against the mounted MPQ.
struct ForgeRewardRules {
    std::array<std::array<std::string,7>,3> gems;
    std::array<std::array<std::string,11>,3> runes;
};
}

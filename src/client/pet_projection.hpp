#pragma once
#include "contracts/hireling.hpp"
#include <cstdint>
#include <vector>

namespace d2x {
struct ClassicData;
struct OnlineWorldView;
std::vector<PetPortraitView> projectPetPortraits(const ClassicData &, const OnlineWorldView &, uint32_t owner);
} // namespace d2x

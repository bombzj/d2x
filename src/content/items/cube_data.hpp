#pragma once
#include "content/classic_data.hpp"

namespace d2x {
void loadCubeData(ClassicData &);
void freezeCubeItem(const ClassicData &, ItemInstance &, uint64_t *generationRandom = nullptr);
void applyCubeProperties(const ClassicData &, const CubeOutput &, ItemInstance &, uint64_t &);
void updateCubeRequiredLevel(const ClassicData &, ItemInstance &);
} // namespace d2x

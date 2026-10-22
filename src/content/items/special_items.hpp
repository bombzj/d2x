#pragma once
#include "content/classic_data.hpp"

namespace d2x {
void loadSpecialItemData(ClassicData &data);
void loadSetItemBonuses(const ClassicData &, const DataTable &, size_t row, SpecialItemRecord &);
} // namespace d2x

#pragma once
#include "classic_data.hpp"

namespace d2x {
void loadPropertyData(ClassicData &data);
bool isDirectPropertyRoll(const ClassicData &data, std::string_view code);
} // namespace d2x

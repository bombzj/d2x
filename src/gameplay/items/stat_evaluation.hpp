#pragma once
#include "modifiers.hpp"

namespace d2x {
ResolvedItemStat evaluateItemStat(const ItemStatDefinition &definition, int rawValue,
                                 int layer, int characterLevel);
}
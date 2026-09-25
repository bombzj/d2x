#pragma once
#include "classic_data.hpp"
#include <optional>
#include <span>

namespace d2x {
// Shared purchase, sale and repair quote. Sale uses NPC buy rates and difficulty caps.
// Missing rule data defers the quote.
std::optional<unsigned> itemTradePrice(const ClassicData &data, const ItemInstance &item,
                                       const VendorDefinition &vendor, bool repair = false,
                                       std::span<const int> questFactors = {}, int reducedPrices = 0,
                                       bool sale = false, int difficulty = 0);
}

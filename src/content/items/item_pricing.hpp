#pragma once
#include "content/classic_data.hpp"
#include <optional>
#include <span>

namespace d2x {
struct ItemInstance;
// Shared purchase, sale and repair quote. Sale uses NPC buy rates and difficulty caps.
// Missing rule data defers the quote.
std::optional<unsigned> itemTradePrice(const ClassicData &data, const ItemInstance &item,
                                       const VendorDefinition &vendor, bool repair = false,
                                       std::span<const int> questFactors = {}, int reducedPrices = 0,
                                       bool sale = false, int difficulty = 0, unsigned autoAffix = 0,
                                       std::optional<int64_t> bodyCost = {}, std::span<const int> repairQuestFactors = {},
                                       std::optional<unsigned> bookRow = {});
std::optional<unsigned> itemGamblePrice(const ClassicData &, std::string_view code, int level, unsigned format, int reducedPrices);
}

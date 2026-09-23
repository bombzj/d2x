#pragma once
#include "content/classic_data.hpp"
#include "gameplay/items/state.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace d2x {
struct VendorOffer {
    uint32_t slot = 0;
    std::string code;
    unsigned quantity = 1, level = 1, price = 0;
    int defense = 0;
    bool permanent = false;
};
std::vector<VendorOffer> planVendorStock(const ClassicData &data, const VendorDefinition &vendor,
                                         unsigned playerLevel, uint64_t seed);
} // namespace d2x

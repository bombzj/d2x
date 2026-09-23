#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/data_table.hpp"
#include <map>
#include <string>
#include <vector>

namespace d2x {
struct VendorItemRule {
    std::string code;
    int level = 0;
    int minimum = 0, maximum = 0;
    int magicMinimum = 0, magicMaximum = 0, magicLevel = 255;
    bool permanent = false, magicEligible = false;
};
struct VendorDefinition {
    std::string id;
    int sellMultiplier = 0;
    std::vector<VendorItemRule> items;
};
std::map<std::string, VendorDefinition, std::less<>> loadVendorData(
    const std::map<std::string, DataTable, std::less<>> &tables, const ItemCatalog &catalog);
} // namespace d2x

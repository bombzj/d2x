#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/data_table.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace d2x {
struct VendorItemRule {
    std::string code;
    int level = 0;
    int minimum = 0, maximum = 0;
    int magicMinimum = 0, magicMaximum = 0, magicLevel = 255;
    int storePage = -1;
    bool permanent = false, magicEligible = false;
};
struct VendorDefinition {
    struct QuestPrice { int flag = 0, sell = 1024, buy = 1024, repair = 1024; };
    std::string id;
    int sellMultiplier = 0;
    int buyMultiplier = 0;
    std::array<int, 3> maxBuy{};
    std::vector<VendorItemRule> items;
    int repairMultiplier = 0;
    unsigned act = 0;
    std::vector<QuestPrice> questPrices;
};
std::map<std::string, VendorDefinition, std::less<>> loadVendorData(
    const std::map<std::string, DataTable, std::less<>> &tables, const ItemCatalog &catalog);
} // namespace d2x

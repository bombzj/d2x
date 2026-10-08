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
    int storePage = -1;
    bool permanent = false;
    ItemGeneration generation;
    std::string displayCode; // Gambling hides the generated quality and upgraded base.
};
ItemInstance vendorItem(const VendorOffer &offer, const ClassicData &data, bool hidden = false);
bool npcCanRepair(std::string_view npcClass);
bool npcCanIdentify(std::string_view npcClass);
bool npcCanHeal(std::string_view npcClass);
bool npcCanGamble(std::string_view npcClass);
std::vector<VendorOffer> planGambleStock(const ClassicData &,unsigned playerLevel,int difficulty,uint64_t &seed,
    const std::set<size_t> &usedUniques,std::string_view characterClass);
std::vector<VendorOffer> planVendorStock(const ClassicData &data, const VendorDefinition &vendor,
                                         unsigned playerLevel, int difficulty, uint64_t seed);
} // namespace d2x

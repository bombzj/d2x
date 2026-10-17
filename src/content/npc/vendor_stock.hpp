#pragma once
#include "content/classic_data.hpp"
#include "gameplay/items/state.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace d2x {
bool npcCanRepair(std::string_view npcClass);
bool npcCanIdentify(std::string_view npcClass);
bool npcCanHeal(std::string_view npcClass);
bool npcCanGamble(std::string_view npcClass);
using VendorItemSource=std::function<bool(std::string_view,ItemQuality,unsigned,bool)>;
using GambleItemSource=std::function<bool(std::string_view,std::string_view,ItemQuality,unsigned)>;
void generateGambleStock(const ClassicData &,unsigned playerLevel,int difficulty,uint64_t &random,const GambleItemSource &create);
void generateVendorStock(const ClassicData &data,const VendorDefinition &vendor,
    unsigned playerLevel,int difficulty,uint64_t &random,const VendorItemSource &create);
} // namespace d2x

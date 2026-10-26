#pragma once
#include "gameplay/loot/plan.hpp"
namespace d2x {
struct ClassicData;
namespace server::loot {struct Preparation;}
LootPlan planPd2LucionChest(const ClassicData &,const server::loot::Preparation &,const ItemSeedSource &);
}

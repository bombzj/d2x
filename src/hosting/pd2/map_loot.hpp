#pragma once
#include "gameplay/loot/plan.hpp"
namespace d2x {struct ClassicData;struct MonsterLootEntry;struct ItemInstance;}
namespace d2x::pd2 {struct MapProperties;}
namespace d2x::server::loot {struct Preparation;}
namespace d2x {
LootPlan planPd2MapLoot(const ClassicData &,const server::loot::Preparation &,const MonsterLootEntry &,const ItemSeedSource &);
void preparePd2MapItemDrop(const pd2::MapProperties &,LootDrop &,uint32_t &programRandom);
void finishPd2MapItemDrop(const ClassicData &,const pd2::MapProperties &,ItemInstance &,uint64_t &itemRandom,uint32_t &programRandom);
}

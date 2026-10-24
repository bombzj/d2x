#pragma once
#include "gameplay/items/state.hpp"
namespace d2x {
struct ClassicData;
ItemInstance rerollPd2DemonicItem(const ClassicData &,const ItemInstance &,uint64_t &,int difficulty);
}

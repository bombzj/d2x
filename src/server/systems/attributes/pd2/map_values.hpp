#pragma once
#include <cstdint>
namespace d2x::pd2 {struct MapInstance;}
namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::attributes {
void preparePd2MapMonster(monsters::Actor &,const pd2::MapInstance &,uint64_t tick,bool dead);
}

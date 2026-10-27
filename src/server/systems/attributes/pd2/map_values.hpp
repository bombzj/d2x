#pragma once
#include <cstdint>
#include <span>
#include "gameplay/stats/stat_list.hpp"
namespace d2x::pd2 {struct MapInstance;}
namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::attributes {
void preparePd2MapMonster(monsters::Actor &,const pd2::MapInstance &,uint64_t tick,bool dead,unsigned participants,std::span<const StatValue> runtimeGlobal={});
void preparePd2SceneMonster(monsters::Actor &,int lifePercent,uint64_t tick);
void preparePd2EventMonster(monsters::Actor &,int velocity,uint64_t tick,int lifeFactor=0);
void preparePd2HiddenSceneMonster(monsters::Actor &,uint64_t tick);
void preparePd2BossScaling(monsters::Actor &,const pd2::MapInstance &,uint64_t tick,bool dead,unsigned participants);
}

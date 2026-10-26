#pragma once
#include <cstdint>
#include <span>

namespace d2x::pd2 {
int selectMapController(int level,int quality,bool force,uint32_t &random);
int mapEventRoomGoal(int level);
bool mapEventRoomEligible(int level);
void shuffleMapEvent(std::span<int>,uint32_t seed);
std::span<const int> mendelnPool(bool fallback=false);
std::span<const int> wandererPool(bool actBoss);
}

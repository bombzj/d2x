#pragma once
#include "core/math.hpp"
#include "core/id.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include <span>
#include <cmath>
#include "core/random.hpp"
#include <algorithm>
#include <vector>
namespace d2x {
// MISSILES_CreateMissileFromParams: seed from the owner's base pierce_idx;
// stat absent means zero. This stream does not consume combat/action RNG.
inline int missilePierceCount(int chance,uint32_t ownerIndex) {
    auto random=initialRandom(ownerIndex);int count=0;
    while(count<4 && limitedRandom(random,100)<unsigned(std::clamp(chance,0,100))) ++count;
    return count;
}
struct MissileTargetBurst { int count{},countPerLevel{},radius{}; };
struct MissileBurstTarget { EntityId id; Vec position; };
// Native aura callbacks keep the caller's unit traversal order. Visibility,
// allegiance and collision remain separate client/server adapter decisions.
inline std::vector<MissileBurstTarget> missileBurstTargets(Vec origin,int radius,int count,std::span<const MissileBurstTarget> candidates) {
    std::vector<MissileBurstTarget> result;
    if(radius<=0 || radius>255 || count<=0) return result;
    for(const auto &target:candidates) {
        const Vec delta{std::floor(target.position.x)-std::floor(origin.x),std::floor(target.position.y)-std::floor(origin.y)};
        if(delta.x*delta.x+delta.y*delta.y<=float(radius*radius)) {result.push_back(target);if(int(result.size())==count) break;}
    }
    return result;
}
struct PoisonCloudDirection { Vec direction; bool secondary{}; };
// MissMode::CreatePoisonCloudHitSubmissiles / retail client equivalent.
inline std::vector<PoisonCloudDirection> poisonCloudDirections(int mainStep,int subStep) {
    constexpr Vec offsets[16]{{0,2},{1,2},{2,2},{2,1},{2,0},{2,-1},{2,-2},{1,-2},
        {0,-2},{-1,-2},{-2,-2},{-2,-1},{-2,0},{-2,1},{-2,2},{-1,2}};
    std::vector<PoisonCloudDirection> result;
    for(int i=0;i<16;i+=std::max(1,mainStep)) result.push_back({offsets[i],false});
    if(subStep>0) for(int i=0;i<15;i+=subStep) result.push_back({offsets[i+1],true});
    return result;
}
inline int poisonCloudVelocity(int parameter) { return parameter*128*75/100; }
// D2Game CreateImmolationArrowHitSubmissiles and retail D2Client B9740.
inline std::vector<Vec> missileDiskOffsets(int radius) {
    std::vector<Vec> result;
    if(radius<0 || radius>255) return result;
    for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y)
        if(x*x+y*y<=radius*radius) result.push_back({float(x),float(y)});
    return result;
}
} // namespace d2x

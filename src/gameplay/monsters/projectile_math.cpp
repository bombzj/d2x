#include "projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace d2x {
std::vector<Vec> monsterQuillTargets(Vec target,int count,uint64_t &random) {
    std::vector<Vec> result; int x=5,y=5;
    for (int i=0;i<std::clamp(count,0,255);++i) {
        if (rollRandom(random)&1) x=-x;
        if (rollRandom(random)&1) y=-y;
        result.push_back(target+Vec{float(x),float(y)});
    }
    return result;
}
int monsterFacing8(Vec from,Vec to) {
    const int dx=int(std::floor(to.x))-int(std::floor(from.x)),dy=int(std::floor(to.y))-int(std::floor(from.y));
    const int x=std::abs(dx),y=std::abs(dy);constexpr std::array thresholds{13,26,39,53,68,85,105};
    const int tangent=std::max(x,y)?127*std::min(x,y)/std::max(x,y):0;
    int angle=int(std::upper_bound(thresholds.begin(),thresholds.end(),tangent)-thresholds.begin());
    if(x>y) angle=(-1-angle)&15;
    if(dy<0) angle=(-1-angle)&31;
    if(dx>=0) angle=(-1-angle)&63;
    return ((((angle+8)&63)+4)>>3)&7;
}
Vec monsterDirectionOffset(int index) {
    constexpr std::array x{0,-1,-1,-1,0,1,1,1,0,-1,-2,-2,-2,-2,-2,-1,0,1,2,2,2,2,2,1,0,-3,-3,-3,0,3,3,3};
    constexpr std::array y{-1,-1,0,1,1,1,0,-1,-2,-2,-2,-1,0,1,2,2,2,2,2,1,0,-1,-2,-2,-3,-3,0,3,3,3,0,-3};
    return {float(x.at(size_t(index))),float(y.at(size_t(index)))};
}
std::pair<Vec,Vec> monsterWebTrailPoints(Vec position,Vec movement) {
    constexpr std::array directions{10,8,22,20,18,16,14,12};
    const auto offset=monsterDirectionOffset(directions[size_t(monsterFacing8(position-movement,position))]);
    const Vec origin{std::floor(position.x)+offset.x,std::floor(position.y)+offset.y};
    return {origin,origin+offset*2.f};
}
}

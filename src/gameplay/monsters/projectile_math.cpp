#include "projectile_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace d2x {
std::vector<MonsterEnchantmentRay> monsterLightningRays() {
    std::vector<MonsterEnchantmentRay> rays;
    for(const Vec direction:std::array{Vec{0,-1},Vec{1,0},Vec{0,1},Vec{-1,0}})
        for(int index=0;index<2;++index) rays.push_back({direction,index});
    return rays;
}
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
std::pair<Vec,Vec> andarielSprayRay(Vec position,Vec target,int frame) {
    constexpr std::array<int,72> rays{27,14,15,3,99,7,21,22,31,26,12,13,2,99,6,19,20,30,
        25,10,11,1,99,5,17,18,29,24,8,9,0,99,4,15,16,28,
        31,22,23,7,99,3,13,14,27,30,20,7,6,99,2,1,12,26,
        29,18,19,5,99,1,9,10,25,28,16,17,4,99,0,23,8,24};
    constexpr std::array origins{29,28,27,26,25,24,31,30};
    const auto facing=size_t(monsterFacing8(position,target));
    const Vec origin{std::floor(position.x),std::floor(position.y)};
    const auto ray=rays[facing*9+size_t(std::clamp(frame-4,0,8))];
    const Vec aim=origin+monsterDirectionOffset(origins[facing])+(ray==99?Vec{}:monsterDirectionOffset(ray));
    return {origin,aim};
}
std::pair<Vec,Vec> gargoyleTrapRay(Vec position,Vec target,GargoyleRaySide side) {
    const int x=int(std::floor(position.x)),y=int(std::floor(position.y));
    int dx=int(std::floor(target.x))-x,dy=int(std::floor(target.y))-y;
    if(std::abs(dx)<std::abs(dy)) {dx=std::clamp(dx,-4,4);if(side==GargoyleRaySide::Client) dy=0;}
    else {dy=std::clamp(dy,-4,4);if(side==GargoyleRaySide::Client) dx=0;}
    const Vec origin{float(x+dx/6-1),float(y+dy/6-1)};
    return {origin,origin+Vec{float(dx),float(dy)}};
}
}

#include "amazon_missile.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
int missilePierceCount(int chance,uint32_t ownerIndex) {
    auto random=initialRandom(ownerIndex);int count=0;
    while(count<4 && limitedRandom(random,100)<unsigned(std::clamp(chance,0,100))) ++count;
    return count;
}

std::vector<MissileBurstTarget> missileBurstTargets(Vec origin,int radius,int count,std::span<const MissileBurstTarget> candidates) {
    std::vector<MissileBurstTarget> result;
    if(radius<=0 || radius>255 || count<=0) return result;
    for(const auto &target:candidates) {
        const Vec delta{std::floor(target.position.x)-std::floor(origin.x),std::floor(target.position.y)-std::floor(origin.y)};
        if(delta.x*delta.x+delta.y*delta.y<=float(radius*radius)) {result.push_back(target);if(int(result.size())==count) break;}
    }
    return result;
}

std::vector<PoisonCloudDirection> poisonCloudDirections(int mainStep,int subStep) {
    constexpr Vec offsets[16]{{0,2},{1,2},{2,2},{2,1},{2,0},{2,-1},{2,-2},{1,-2},
        {0,-2},{-1,-2},{-2,-2},{-2,-1},{-2,0},{-2,1},{-2,2},{-1,2}};
    std::vector<PoisonCloudDirection> result;
    for(int i=0;i<16;i+=std::max(1,mainStep)) result.push_back({offsets[i],false});
    if(subStep>0) for(int i=0;i<15;i+=subStep) result.push_back({offsets[i+1],true});
    return result;
}

int poisonCloudVelocity(int parameter) { return parameter*128*75/100; }

std::vector<Vec> missileDiskOffsets(int radius) {
    std::vector<Vec> result;
    if(radius<0 || radius>255) return result;
    for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y)
        if(x*x+y*y<=radius*radius) result.push_back({float(x),float(y)});
    return result;
}
} // namespace d2x

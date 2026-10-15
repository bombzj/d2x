#include "targeting.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cmath>
#include <cstdint>

namespace d2x {
bool withinNativeAuraArea(Vec origin,Vec target,int32_t radius) {
    if(radius<0) return false;
    const auto dx=nativeSubtract(int32_t(target.x),int32_t(origin.x));
    const auto dy=nativeSubtract(int32_t(target.y),int32_t(origin.y));
    return nativeAdd(nativeMultiply(dx,dx),nativeMultiply(dy,dy))<=nativeMultiply(radius,radius);
}
bool withinSkillArea(Vec origin, Vec target, float radius, SkillAreaCoordinates coordinates) {
    if (coordinates == SkillAreaCoordinates::Continuous) return (target-origin).length() <= radius;
    if (coordinates == SkillAreaCoordinates::FlooredCells) {
        const float dx=std::floor(target.x)-std::floor(origin.x);
        const float dy=std::floor(target.y)-std::floor(origin.y);
        return dx*dx+dy*dy <= radius*radius;
    }
    const int64_t dx=int64_t(int(target.x))-int(origin.x);
    const int64_t dy=int64_t(int(target.y))-int(origin.y);
    return dx*dx+dy*dy <= double(radius)*radius;
}
bool skillAiControlAllowed(MonsterRank rank, bool switchAi, bool boss) {
    return switchAi && !boss && (rank==MonsterRank::Normal || rank==MonsterRank::Minion);
}
}

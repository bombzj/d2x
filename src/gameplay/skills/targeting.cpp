#include "targeting.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cmath>
#include <cstdint>

namespace d2x {
EntityId nativeSkillSuccessor(std::span<const EntityId> candidates,uint32_t previous,const NativeUnitIds &identities) {
    uint32_t larger=UINT32_MAX,wrapped=previous;
    EntityId next,first;
    for(const auto id:candidates) {
        const auto guid=identities.guid(id);
        if(guid>previous && guid<larger) {larger=guid;next=id;}
        if(guid<=wrapped) {wrapped=guid;first=id;}
    }
    return next?next:first;
}
EntityId nativeMissileSuccessor(std::span<const EntityId> candidates,EntityId hit,const NativeUnitIds &identities) {
    // MISSMODE_SrvHit12 uses the same aura callback as skills, then rejects
    // the hit unit. Do not exclude it before selection: equal GUIDs of other
    // types still participate, with the native last wrapped tie winning.
    const auto selected=nativeSkillSuccessor(candidates,identities.guid(hit),identities);
    return selected==hit?EntityId{}:selected;
}
EntityId nativeSkillSuccessor(std::span<const EntityId> candidates,uint32_t previous) {
    uint32_t larger=UINT32_MAX,wrapped=previous;bool hasLarger=false,hasWrapped=false;
    for(const auto id:candidates) {
        const auto guid=uint32_t(id.value);
        if(guid>previous && guid<larger) {larger=guid;hasLarger=true;}
        if(guid<=wrapped) {wrapped=guid;hasWrapped=true;}
    }
    return hasLarger?EntityId{larger}:hasWrapped?EntityId{wrapped}:EntityId{};
}
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

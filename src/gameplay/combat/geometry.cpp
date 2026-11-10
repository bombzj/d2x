#include "gameplay/combat/geometry.hpp"
#include "gameplay/units/distance.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "world/navigation.hpp"
#include "world/native_spawn.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
Vec knockbackDestination(Vec target, Vec source, int distance) {
    const int targetX=int(std::floor(target.x+.5f)),targetY=int(std::floor(target.y+.5f));
    const int x=targetX-int(std::floor(source.x+.5f));
    const int y=targetY-int(std::floor(source.y+.5f));
    const int divisor=std::max(std::abs(x),std::abs(y));
    if(!divisor || distance<=0) return target;
    return {float(targetX+x*distance/divisor),float(targetY+y*distance/divisor)};
}
std::optional<float> missileTerrainContact(const Grid &grid, Vec from, Vec to, MissileCollisionRule rule, Vec origin) {
    if (grid.missileSegment(from - origin, to - origin, rule)) return {};
    float clear = 0, blocked = 1;
    for (int step = 0; step < 12; ++step) {
        const float middle = (clear + blocked) * .5f;
        if (grid.missileSegment(from - origin, from + (to - from) * middle - origin, rule)) clear = middle;
        else blocked = middle;
    }
    return clear;
}
int meleeDistance(Vec from, int fromSize, Vec to, int toSize) {
    return nativeUnitDistance(from,fromSize,to,toSize);
}
bool playerMeleeReach(const Grid &grid, Vec from, Vec to, int targetSize, int rangeAdder) {
    // Units.cpp::UNITS_IsInMeleeRange: zero distance is already in range;
    // otherwise test the shortened PLAYER_FLYING ray, not a walking footprint.
    return unitMeleeReach(grid,from,2,to,targetSize,rangeAdder);
}
bool unitMeleeReach(const Grid &grid,Vec from,int sourceSize,Vec to,int targetSize,int range,int rangeBonus) {
    const int distance=nativeUnitDistance(from,sourceSize,to,targetSize);
    return distance<=0 || (distance<=range+rangeBonus+1 && grid.interactionSegment(from,to,targetSize,{}));
}
int missileDistance(Vec from, Vec to) {
    return unitCoordinateDistance(int(std::floor(from.x)), int(std::floor(from.y)),
        int(std::floor(to.x)), int(std::floor(to.y)));
}
std::optional<Vec> nativeLeapLanding(const Grid &terrain,const Grid &occupancy,Vec from,Vec target,int range,int size) {
    const int x=int(std::floor(from.x)),y=int(std::floor(from.y));
    int tx=int(std::floor(target.x)),ty=int(std::floor(target.y));
    const int distance=std::max(1,unitCoordinateDistance(x,y,tx,ty));
    if(distance>range) {
        tx=x+nativeMultiply(tx-x,range)/distance;
        ty=y+nativeMultiply(ty-y,range)/distance;
    }
    const int dx=std::clamp(ty-y,-2,2),dy=std::clamp(x-tx,-2,2);
    const Vec start{float(x),float(y)};
    for(const auto &[cx,cy]:{std::pair{tx,ty},std::pair{tx+dx,ty+dy},std::pair{tx-dx,ty-dy}}) {
        const auto free=nativeSpawnCoordinate(cx,cy,50,[&](int column,int line){return occupancy.walkable(column,line,{0x1c09,size});});
        if(!free || unitCoordinateDistance(x,y,free->first,free->second)>range) continue;
        const Vec landing{float(free->first),float(free->second)};
        if(terrain.collisionSegment(start,landing,0x804)) return landing;
    }
    return {};
}
std::optional<Vec> nativeLeapAttackLanding(const Grid &terrain,const Grid &occupancy,Vec from,Vec target,int size) {
    const int x=int(std::floor(from.x)),y=int(std::floor(from.y));
    const int tx=int(std::floor(target.x)),ty=int(std::floor(target.y));
    const int distance=unitCoordinateDistance(x,y,tx,ty);
    if(!distance) return {};
    const auto clear=[&](int column,int row){return occupancy.walkable(column,row,{0x1c09,size});};
    int lx=tx+2*(tx-x)/distance,ly=ty+2*(ty-y)/distance;
    if(!clear(lx,ly)) {
        const auto free=nativeSpawnCoordinate(tx,ty,50,clear);
        if(!free) return {};
        lx=free->first;ly=free->second;
    }
    const Vec landing{float(lx),float(ly)};
    if(!terrain.collisionSegment({float(x),float(y)},landing,0x804)) return {};
    return landing;
}
std::optional<float> missileUnitIntersection(Vec from, Vec to, int missileSize, Vec unit, int unitSize) {
    if (missileSize < 0 || missileSize > 3 || unitSize <= 0 || unitSize > 3) return std::nullopt;
    // Path.cpp: size 1/2 units occupy a cross, size 3 a square. Missile size
    // uses COLLISION_CheckMaskWithSize (point/cross/square) instead.
    float first = 2.f;
    const Vec delta = to - from;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            if (unitSize < 3 && x && y) continue;
            for (int my = -1; my <= 1; ++my)
                for (int mx = -1; mx <= 1; ++mx) {
                    if (missileSize <= 1 && (mx || my)) continue;
                    if (missileSize == 2 && mx && my) continue;
                    const float left = std::floor(unit.x) + x - mx;
                    const float top = std::floor(unit.y) + y - my;
                    float enter = 0.f, leave = 1.f;
                    auto axis = [&](float position, float motion, float minimum) {
                        if (std::abs(motion) < .000001f) return position >= minimum && position < minimum + 1;
                        float a = (minimum - position) / motion, b = (minimum + 1 - position) / motion;
                        if (a > b) std::swap(a, b);
                        enter = std::max(enter, a); leave = std::min(leave, b);
                        return enter <= leave;
                    };
                    if (axis(from.x, delta.x, left) && axis(from.y, delta.y, top)) first = std::min(first, enter);
                }
        }
    return first <= 1.f ? std::optional<float>(first) : std::nullopt;
}
} // namespace d2x

#include "gameplay/combat/geometry.hpp"
#include "gameplay/units/distance.hpp"
#include "world/navigation.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
Vec knockbackDestination(Vec target, Vec source, int distance) {
    const int x=int(std::floor(target.x))-int(std::floor(source.x));
    const int y=int(std::floor(target.y))-int(std::floor(source.y));
    const int divisor=std::max(std::abs(x),std::abs(y));
    if(!divisor || distance<=0) return target;
    return {std::floor(target.x)+float(x*distance/divisor)+.5f,std::floor(target.y)+float(y*distance/divisor)+.5f};
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
    const int distance=nativeUnitDistance(from,2,to,targetSize);
    return distance<=0 || (distance<=rangeAdder+1 && grid.interactionSegment(from,to,targetSize,{}));
}
int missileDistance(Vec from, Vec to) {
    return unitCoordinateDistance(int(std::floor(from.x)), int(std::floor(from.y)),
        int(std::floor(to.x)), int(std::floor(to.y)));
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

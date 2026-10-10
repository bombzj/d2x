#include "projectile_path.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace d2x {
std::vector<Vec> blessedHammerPath(Vec origin) {
    std::vector<Vec> path;path.reserve(77);
    Vec previous{std::floor(origin.x),std::floor(origin.y)};
    for(int sample=1;path.size()<77;++sample) {
        const float angle=float((sample*16)&511)*6.2831853071795864769f/512.f;
        const float distance=float(sample*9600)/65536.f;
        Vec next{std::floor(origin.x+std::cos(angle)*distance),std::floor(origin.y+std::sin(angle)*distance)};
        if(next.x==previous.x && next.y==previous.y) continue;
        path.push_back(next+Vec{.5f,.5f});previous=next;
    }
    return path;
}
std::optional<Vec> missileGuidedDirection(Vec position,Vec target,int remaining,int period) {
    if(!missileEmissionDue(remaining,period)) return {};
    const int distance=missileDistance(position,target);
    if(distance<=3 || distance>=25) return {};
    return (target-position).unit();
}
bool missileChangedCell(Vec previous, Vec next) {
    return int(previous.x)!=int(next.x) || int(previous.y)!=int(next.y);
}
std::optional<int> missileVelocityFixed(int base, int perLevel, int rank,int slowPercent) {
    if (rank < 1) return {};
    const auto velocity=int64_t(base)+int64_t(rank)*perLevel/8;
    // 256 * 75 / 100 is exactly 192; check before multiplying.
    if (velocity<0 || velocity>std::numeric_limits<int>::max()/192) return {};
    const int64_t fixed=velocity*256;
    return int((slowPercent?fixed*std::clamp(slowPercent,0,100)/100:fixed)*75/100);
}
Vec missileWallDirection(Vec caster, Vec target) {
    return {std::floor(target.y)-std::floor(caster.y),std::floor(caster.x)-std::floor(target.x)};
}
uint64_t missileChainSuccessor(uint64_t hit, std::span<const uint64_t> eligible) {
    uint64_t next=0,first=0;
    for(const auto id:eligible) {
        if(!id || id==hit) continue;
        if(!first || id<first) first=id;
        if(id>hit && (!next || id<next)) next=id;
    }
    return next?next:first;
}
Vec blizzardOffset(uint32_t globalX, int remaining, int radius, bool client) {
    if(radius<=1) return {};
    auto random=initialRandom(globalX+uint32_t(remaining));
    const int range=radius-1;
    Vec offset{float(int(limitedRandom(random,uint32_t(2*range)))-range),
               float(int(limitedRandom(random,uint32_t(2*range)))-range)};
    return client?offset*-1.f:offset;
}
Vec missileRingDirection(int index) {
    constexpr int offsets[]{30,29,29,28,27,26,24,23,21,19,16,14,11,8,5,2,
        0,-2,-5,-8,-11,-14,-16,-19,-21,-23,-24,-26,-27,-28,-29,-29,
        -30,-29,-29,-28,-27,-26,-24,-23,-21,-19,-16,-14,-11,-8,-5,-2,
        0,2,5,8,11,14,16,19,21,23,24,26,27,28,29,29};
    const unsigned direction = unsigned(index) & 63;
    return {float(offsets[direction]), float(offsets[(direction + 48) & 63])};
}
bool missileEmissionDue(int remaining, int period) {
    return remaining > 0 && period > 0 && remaining % period == 0;
}
std::optional<MissileRingEmission> missileRingEmission(int remaining, int period, int index, int step) {
    if (!missileEmissionDue(remaining, period)) return {};
    return MissileRingEmission{missileRingDirection(index), (index + step) & 63};
}
std::optional<Vec> missileOrbTurn(Vec target, int remaining, int window, int period) {
    if (remaining >= window || !missileEmissionDue(remaining, period)) return {};
    return missileDiagonalTurn(target);
}
std::vector<Vec> missileRingBurst(int step) {
    std::vector<Vec> result;
    if (step <= 0) return result;
    for (int direction = 0; direction < 64; direction += step) result.push_back(missileRingDirection(direction));
    return result;
}
Vec missileDiagonalTurn(Vec target) {
    const int x = int(target.x), y = int(target.y);
    return {float((x - y) / 2), float((x + y) / 2)};
}
std::vector<Vec> missileFanTargets(Vec origin, Vec target, int count, Vec facing) {
    // SKILLS_SrvDo008: integer perpendicular normalization, not angular interpolation.
    int dx = int(target.x) - int(origin.x), dy = int(target.y) - int(origin.y);
    if (!dx && !dy) { dx = int(std::round(facing.x * 4)); dy = int(std::round(facing.y * 4)); }
    auto squared = [&] { return int64_t(dx) * dx + int64_t(dy) * dy; };
    if (squared() < 4) { dx *= 4; dy *= 4; }
    if (squared() < 16) { dx *= 2; dy *= 2; }
    int sideX = dy, sideY = -dx;
    while (int64_t(sideX) * sideX + int64_t(sideY) * sideY > 3) { sideX /= 2; sideY /= 2; }
    Vec endpoint{float(int(target.x) - count * sideX / 2) + .5f,
                 float(int(target.y) - count * sideY / 2) + .5f};
    std::vector<Vec> result;
    for (int index = 0; index < count; ++index) {
        result.push_back(endpoint); endpoint = endpoint + Vec{float(sideX), float(sideY)};
    }
    return result;
}
std::vector<Vec> chargedBoltPath(Vec origin, Vec target, int index, int frames) {
    int deltaX = int(target.x) - int(origin.x), deltaY = int(target.y) - int(origin.y);
    const int absX = std::abs(deltaX), absY = std::abs(deltaY);
    int directionIndex = -1;
    if (absX < 2 * absY) {
        if (absY >= 2 * absX) {
            if (deltaX < 0) directionIndex = deltaY < -1 ? 5 : std::min(deltaY, 2) + 7;
            else deltaX &= 1;
        }
    } else deltaY = deltaY >= 0 ? deltaY & 1 : -1;
    if (directionIndex < 0) {
        deltaX = std::clamp(deltaX, -2, 2);
        directionIndex = deltaY < -1 ? 5 * deltaX + 10 : std::min(deltaY, 2) + 5 * deltaX + 12;
    }
    constexpr int directions[]{5,4,4,4,3,6,5,4,3,2,6,6,6,2,2,6,7,0,1,2,7,0,0,0,1};
    constexpr Vec offsets[]{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    const int mainDirection = directions[directionIndex];
    // SkillSor.cpp SKILLS_MissileInit_ChargedBolt deliberately seeds from bolt index + path X.
    uint64_t seed = initialRandom(uint32_t(index + int(target.x)));
    Vec point{float(int(origin.x)) + .5f, float(int(origin.y)) + .5f};
    std::vector<Vec> path{point};
    for (int step = 0; step < std::min(77, frames) / 2; ++step) {
        rollRandom(seed);
        const int roll = int(uint32_t(seed) & 31);
        const int offset = roll == 31 ? 1 : roll % 3 - 1;
        point = point + offsets[(mainDirection + offset + 8) % 8] * 2.f;
        path.push_back(point);
    }
    return path;
}
} // namespace d2x

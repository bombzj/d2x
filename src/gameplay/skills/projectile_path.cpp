#include "projectile_path.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <bit>
#include <limits>

namespace d2x {
std::vector<Vec> blessedHammerPath(Vec origin) {
    // Retail Fog ordinals 10083/10084 index the same 512-entry float table.
    // Hammer advances by 16 entries. Preserve those exact float words and
    // the x87 product before truncation, rather than host libm rounding.
    constexpr std::array<uint32_t,32> sine{
        0x00000000u,0x3e47c5c2u,0x3ec3ef16u,0x3f0e39dau,0x3f3504f3u,0x3f54db32u,0x3f6c835fu,0x3f7b14bfu,
        0x3f800000u,0x3f7b14beu,0x3f6c835eu,0x3f54db31u,0x3f3504f2u,0x3f0e39d9u,0x3ec3ef13u,0x3e47c5bcu,
        0xb3bbbd2eu,0xbe47c5c8u,0xbec3ef18u,0xbf0e39dbu,0xbf3504f4u,0xbf54db32u,0xbf6c835fu,0xbf7b14bfu,
        0xbf800000u,0xbf7b14beu,0xbf6c835eu,0xbf54db30u,0xbf3504f1u,0xbf0e39d8u,0xbec3ef10u,0xbe47c5b7u};
    const auto x=int64_t(std::llround(double(origin.x)*65536)),y=int64_t(std::llround(double(origin.y)*65536));
    std::vector<Vec> path;path.reserve(77);
    Vec previous{std::floor(origin.x),std::floor(origin.y)};
    for(int sample=1;path.size()<77;++sample) {
        const auto index=unsigned(sample)&31;const float distance=float(sample*9600);
        const auto dx=int64_t(double(std::bit_cast<float>(sine[(index+8)&31]))*double(distance));
        const auto dy=int64_t(double(std::bit_cast<float>(sine[index]))*double(distance));
        Vec next{float((x+dx)>>16),float((y+dy)>>16)};
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
    if (rank < 0) return {};
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
Vec blizzardOffset(uint32_t globalX,int elapsed,int radius,bool client,uint64_t &random) {
    random=(random&0xffffffff00000000ULL)|uint32_t(globalX+uint32_t(elapsed));
    const auto range=nativeSubtract(radius,1);
    const auto span=uint32_t(nativeMultiply(2,range));
    Vec offset{float(nativeSubtract(int32_t(limitedRandom(random,span)),range)),
               float(nativeSubtract(int32_t(limitedRandom(random,span)),range))};
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
bool missileEmissionDue(int frame, int period) {
    return frame>=0 && period>0 && frame%period==0;
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
Vec primePoisonNovaDirection(int index) {
    constexpr std::array<Vec,16> directions{{{0,2},{1,2},{2,2},{2,1},{2,0},{2,-1},{2,-2},{1,-2},
        {0,-2},{-1,-2},{-2,-2},{-2,-1},{-2,0},{-2,1},{-2,2},{-1,2}}};
    return directions[unsigned(index)&15];
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
MissileFanLayout pd2MissileFanLayout(Vec origin, Vec target, int count) {
    int deltaX = nativeSubtract(int(origin.x),int(target.x));
    int deltaY = nativeSubtract(int(origin.y),int(target.y));
    int squared = nativeAdd(nativeMultiply(deltaX,deltaX),nativeMultiply(deltaY,deltaY));
    if (squared < 4) {
        deltaX = nativeMultiply(deltaX,4);
        deltaY = squared;
        squared = nativeAdd(nativeMultiply(deltaX,deltaX),nativeMultiply(deltaY,deltaY));
    }
    int sideX = squared < 16 ? nativeMultiply(deltaY,2) : deltaY;
    int sideY = nativeSubtract(0,squared < 16 ? nativeMultiply(deltaX,2) : deltaX);
    while (nativeAdd(nativeMultiply(sideX,sideX),nativeMultiply(sideY,sideY)) > 3) {
        sideX /= 2;
        sideY /= 2;
    }
    return {{float(nativeSubtract(int(target.x),nativeMultiply(sideX,count)/2))+.5f,
             float(nativeSubtract(int(target.y),nativeMultiply(sideY,count)/2))+.5f},
            {float(sideX),float(sideY)}};
}
std::vector<Vec> chargedBoltPath(Vec origin,Vec target,int distance,uint64_t &seed) {
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
    Vec point{float(int(origin.x)) + .5f, float(int(origin.y)) + .5f};
    std::vector<Vec> path{point};
    for (int step = 0; step < distance / 2; ++step) {
        rollRandom(seed);
        const int roll = int(uint32_t(seed) & 31);
        const int offset = roll == 31 ? 1 : roll % 3 - 1;
        point = point + offsets[(mainDirection + offset + 8) % 8] * 2.f;
        path.push_back(point);
    }
    return path;
}
std::vector<Vec> chargedBoltPath(Vec origin,Vec target,int index,int frames) {
    auto random=initialRandom(uint32_t(index+int(target.x)));
    return chargedBoltPath(origin,target,std::min(77,frames),random);
}
std::vector<Vec> missileChargedBurstDirections(int count) {
    // Skills::sub_6FD14260 intentionally emits pairs even for the final odd
    // remainder. Preserve the authored order and that possible extra missile.
    constexpr int signX[]{-1,1,1,-1},signY[]{-1,-1,1,1};
    constexpr int offset1[]{18,20,17,20,15,19,18},offset2[]{8,2,11,4,13,6,9};
    constexpr int xOffsets[]{20,-20,0,0,14,-14,-14,14},yOffsets[]{0,0,20,-20,14,14,-14,-14};
    std::vector<Vec> result{{14,-14}};int remaining=count-1;
    for(int group=0;group<7 && remaining>0;++group) {
        for(int direction=0;direction<4 && remaining>0;++direction) {
            result.push_back({float(offset1[group]*signX[direction]),float(offset2[group]*signY[direction])});
            result.push_back({float(offset2[group]*signX[direction]),float(offset1[group]*signY[direction])});remaining-=2;
        }
        result.push_back({float(xOffsets[group]),float(yOffsets[group])});--remaining;
    }
    return result;
}
} // namespace d2x

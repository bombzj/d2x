#include "projectile_path.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace d2x {
Vec missileRingDirection(int index) {
    constexpr int offsets[]{30,29,29,28,27,26,24,23,21,19,16,14,11,8,5,2,
        0,-2,-5,-8,-11,-14,-16,-19,-21,-23,-24,-26,-27,-28,-29,-29,
        -30,-29,-29,-28,-27,-26,-24,-23,-21,-19,-16,-14,-11,-8,-5,-2,
        0,2,5,8,11,14,16,19,21,23,24,26,27,28,29,29};
    const unsigned direction = unsigned(index) & 63;
    return {float(offsets[direction]), float(offsets[(direction + 48) & 63])};
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

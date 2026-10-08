#include "movement_math.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x {
int monsterAiDistance(Vec from, int size, Vec target) {
    const int x = std::abs(std::abs(int(std::floor(target.x)) - int(std::floor(from.x))) - size);
    const int y = std::abs(std::abs(int(std::floor(target.y)) - int(std::floor(from.y))) - size);
    return std::max(x, y) + std::min(x, y) / 2;
}
Vec monsterWanderPoint(Vec from, int radius, uint64_t &random) {
    radius = std::max(radius, 1);
    const bool horizontal = (rollRandom(random) & 1) != 0;
    const int offset = int(limitedRandom(random, unsigned(radius)));
    int x = horizontal ? radius : offset, y = horizontal ? offset : radius;
    if (rollRandom(random) & 1) x = -x;
    if (rollRandom(random) & 1) y = -y;
    return {std::floor(from.x) + x + .5f, std::floor(from.y) + y + .5f};
}
Vec monsterRetreatPoint(Vec from, Vec target, int distance) {
    const int x = int(std::floor(from.x)), y = int(std::floor(from.y));
    const int tx = int(std::floor(target.x)), ty = int(std::floor(target.y));
    return {float(x + ((x > tx) - (x < tx)) * distance) + .5f,
            float(y + ((y > ty) - (y < ty)) * distance) + .5f};
}
std::array<Vec,3> monsterCirclePoints(Vec from, Vec target, int distance, uint64_t &random) {
    const Vec radial = (from - target).unit();
    const Vec tangent = (rollRandom(random) & 255) < 128 ? Vec{-radial.y, radial.x} : Vec{radial.y, -radial.x};
    std::array<Vec,3> result;
    const std::array<float,3> steps{1.f, .75f, .5f};
    for (size_t i = 0; i < result.size(); ++i) result[i] = target + (radial + tangent * steps[i]).unit() * float(std::max(distance, 1));
    return result;
}
Vec monsterRadiusApproachPoint(Vec from, int size, Vec target, int radius, int targetDistance) {
    const int x = int(std::floor(from.x)), y = int(std::floor(from.y));
    const int dx = int(std::floor(target.x)) - x, dy = int(std::floor(target.y)) - y;
    const int ax = std::abs(dx), ay = std::abs(dy);
    const int difference = monsterAiDistance(from, size, target) - targetDistance;
    const int sign = (difference > 0) - (difference < 0);
    const int advance = std::min(std::abs(difference), std::max(0, radius));
    const int sum = std::max(ax + ay, advance);
    int ox = sum > 0 ? advance * ax / sum : 0, oy = sum > 0 ? advance * ay / sum : 0;
    while (ox + oy < advance) { ++ox; ++oy; }
    return {float(x + sign * ox * ((dx > 0) - (dx < 0))) + .5f,
            float(y + sign * oy * ((dy > 0) - (dy < 0))) + .5f};
}
}

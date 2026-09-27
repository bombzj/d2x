#include "weapon_attack.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
int WeaponAttackTiming::durationTicks() const {
    return std::max(1, ((frames - startFrame) * 256 + speed - 1) / speed - 1);
}
int WeaponAttackTiming::actionTick() const {
    return std::clamp(((actionFrame - startFrame) * 256 + speed - 1) / speed,
                      1, durationTicks());
}
int WeaponAttackState::animationFrame() const {
    return std::clamp(timing.startFrame + ticks * timing.speed / 256, 0, timing.frames - 1);
}
int effectiveAttackSpeed(int animationSpeed, int itemIAS, int baseWeaponSpeed, int skillRate) {
    if (itemIAS <= -120) throw std::invalid_argument("Item IAS exceeds the native supported range");
    const int rate = std::clamp(100 + int(int64_t(120) * itemIAS / (120LL + itemIAS)) +
                              skillRate - baseWeaponSpeed, 15, 175);
    return std::clamp(animationSpeed * rate / 100, 1, 32767);
}
int attackStartingFrame(std::string_view character, std::string_view weapon, std::string_view mode) {
    // UNITS_GetFrameBonus: Amazon/Sorceress skip initial A1/A2 frames.
    if ((character != "ama" && character != "sor") || (mode != "a1" && mode != "a2")) return 0;
    if (weapon == "hth") return 1;
    return weapon == "1hs" || weapon == "1ht" || weapon == "stf" ||
           weapon == "2hs" || weapon == "2ht" ? 2 : 0;
}
int meleeDistance(Vec from, int fromSize, Vec to, int toSize) {
    // D2Common_10399; coordinates are native subtiles, not screen pixels.
    constexpr int distance[64]{
        -1,-1,-1,0,2,4,6,8, -1,-1,0,1,2,4,6,8,
        -1,0,0,2,3,5,7,8, 0,1,2,2,4,5,7,8,
        2,2,3,4,5,6,7,9, 4,4,5,5,6,7,8,9,
        6,6,7,7,7,8,10,10, 8,8,8,8,9,9,10,11};
    const int x = std::abs(int(std::floor(to.x)) - int(std::floor(from.x)));
    const int y = std::abs(int(std::floor(to.y)) - int(std::floor(from.y)));
    if (x >= 8 || y >= 8 || fromSize >= 4 || toSize >= 4) {
        const int size = fromSize / 2 + toSize / 2;
        const int dx = std::max(0, x - size), dy = std::max(0, y - size);
        return 2 * std::max(dx, dy) + std::min(dx, dy);
    }
    int result = distance[x + 8 * y];
    if (result < 0) return 0;
    if (fromSize == 3 || toSize == 3) result = std::max(0, result - 1);
    if (fromSize <= 1 || toSize <= 1) ++result;
    return result;
}
int missileDistance(Vec from, Vec to) {
    const int x = std::abs(int(std::floor(to.x)) - int(std::floor(from.x)));
    const int y = std::abs(int(std::floor(to.y)) - int(std::floor(from.y)));
    return std::max(x, y) + std::min(x, y) / 2;
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

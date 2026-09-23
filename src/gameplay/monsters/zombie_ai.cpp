#include "zombie_ai.hpp"
#include <cstdint>
#include <utility>

namespace d2x {
namespace {
uint32_t nextRandom(Enemy &enemy) {
    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                         (enemy.combatRandom >> 32);
    return uint32_t(enemy.combatRandom);
}
} // namespace

bool zombiePursues(Enemy &enemy, const MonsterAiProfile &rules, float distance) {
    if (enemy.aiPursuing) return true;
    if (distance < float(rules.params[1]) && enemy.aiWait <= 0) {
        enemy.aiPursuing = nextRandom(enemy) % 100 < unsigned(rules.params[0]);
        if (!enemy.aiPursuing) enemy.aiWait = 10.f / 25.f;
    }
    return enemy.aiPursuing;
}

std::optional<Vec> zombieWanderTarget(Enemy &enemy, const Grid &grid) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        int x = 3, y = int(nextRandom(enemy) % 3);
        if (nextRandom(enemy) & 1) std::swap(x, y);
        if (nextRandom(enemy) & 1) x = -x;
        if (nextRandom(enemy) & 1) y = -y;
        const Vec target = enemy.pos + Vec{float(x), float(y)};
        if (grid.walkable(target) && grid.segment(enemy.pos, target)) return target;
    }
    return std::nullopt;
}
} // namespace d2x

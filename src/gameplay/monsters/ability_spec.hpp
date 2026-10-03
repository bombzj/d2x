#pragma once
#include <string>

namespace d2x {
struct MonsterProjectile {
    int id = -1;
    float velocity = 0, lifetime = 0;
    int minimumDamage = 0, maximumDamage = 0, sourceDamage = 0;
};
struct MonsterSpell {
    std::string sourceSkill, mode, art, element;
    MonsterProjectile projectile;
    int minimumDamage = 0, maximumDamage = 0;
    int poisonFrames = 0, hitShift = 8;
    bool killOnHit = true;
};
struct MonsterResurrection {
    std::string sourceSkill, mode, minion;
};
struct MonsterNest {
    std::string sourceSkill, mode, child, sequence;
    int spawnX = 0, spawnY = 0;
};
struct MonsterWeb {
    std::string sourceSkill, mode, art;
    int missileId = -1;
    float lifetime = 0, radius = 0, auraDuration = 0, slowDuration = 0;
    int slowPercent = 0;
};
} // namespace d2x

#pragma once
#include <array>
#include <optional>
#include <string>
#include <utility>

namespace d2x {
struct MonsterAccuracy {
    int level = 1;
    int attackRating = 0;
};
struct MonsterDefense {
    int level = 1;
    int defense = 0;
    bool demon = false, undead = false, boss = false;
};
struct MonsterElementAttack {
    std::string mode, type;
    int chance = 0, minimum = 0, maximum = 0, durationFrames = 0;
};
// Resolved from the original MonStats and MonLvl tables. Some ordinary monsters
// have life but no A1 melee columns, so the attacks are independent.
struct MonsterNormalCombat {
    int minLife = 0, maxLife = 0;
    std::optional<std::pair<int, int>> attack1Damage;
    std::optional<std::pair<int, int>> attack2Damage;
    std::array<std::optional<MonsterElementAttack>, 3> elements;
};
} // namespace d2x

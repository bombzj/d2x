#include "gameplay/monsters/rank.hpp"

namespace d2x {
const char *monsterRankName(MonsterRank rank) {
    switch (rank) {
    case MonsterRank::Normal:
        return "Normal";
    case MonsterRank::Minion:
        return "Minion";
    case MonsterRank::Champion:
        return "Champion";
    case MonsterRank::Unique:
        return "Unique";
    case MonsterRank::SuperUnique:
        return "Super unique";
    case MonsterRank::Boss:
        return "Boss";
    }
    return "Unknown";
}
} // namespace d2x

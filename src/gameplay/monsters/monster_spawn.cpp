#include "gameplay/monsters/monster_spawn.hpp"

namespace d2x {
MonsterImplementation monsterImplementation(const std::string &code) {
    if (code == "fallen1")
        return {MonsterKind::Fallen, false};
    if (code == "zombie1")
        return {MonsterKind::Zombie, false};
    if (code == "skeleton1")
        return {MonsterKind::Skeleton, false};
    if (code == "corruptrogue1")
        return {MonsterKind::CorruptRogue, false};
    return {MonsterKind::Fallen, true};
}
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

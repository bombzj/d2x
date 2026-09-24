#include "gameplay/monsters/monster_spawn.hpp"

namespace d2x {
MonsterImplementation monsterImplementation(const std::string &code) {
    if (code == "fallen1" || code == "fallen2" || code == "fallen3" || code == "fallen4" ||
        code == "fallen5")
        return {MonsterKind::Fallen, false};
    if (code == "zombie1" || code == "zombie2" || code == "zombie3" || code == "zombie4" ||
        code == "zombie5")
        return {MonsterKind::Zombie, false};
    if (code == "skeleton1" || code == "skeleton2" || code == "skeleton3" ||
        code == "skeleton4" || code == "skeleton5")
        return {MonsterKind::Skeleton, false};
    if (code == "corruptrogue1")
        return {MonsterKind::CorruptRogue, false};
    if (code == "brute1" || code == "brute2")
        return {MonsterKind::Brute, false};
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

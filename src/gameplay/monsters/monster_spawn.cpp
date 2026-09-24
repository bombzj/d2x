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
    if (code == "corruptrogue1" || code == "corruptrogue2" || code == "corruptrogue3" ||
        code == "corruptrogue4" || code == "corruptrogue5")
        return {MonsterKind::CorruptRogue, false};
    if (code == "brute1" || code == "brute2" || code == "brute3" || code == "brute4" ||
        code == "brute5")
        return {MonsterKind::Brute, false};
    if (code == "goatman1" || code == "goatman2" || code == "goatman3" || code == "goatman4" ||
        code == "goatman5")
        return {MonsterKind::Goatman, false};
    if (code == "quillrat1" || code == "quillrat2" || code == "quillrat3" ||
        code == "quillrat4" || code == "quillrat5")
        return {MonsterKind::QuillRat, false};
    if (code == "wraith1" || code == "wraith2" || code == "wraith3")
        return {MonsterKind::Wraith, false};
    if (code == "cr_lancer1" || code == "cr_lancer2" || code == "cr_lancer3")
        return {MonsterKind::CorruptLancer, false};
    if (code == "cr_archer1" || code == "cr_archer2" || code == "cr_archer3" ||
        code == "cr_archer4")
        return {MonsterKind::CorruptArcher, false};
    if (code == "sk_archer1" || code == "sk_archer2" || code == "sk_archer3")
        return {MonsterKind::SkeletonBow, false};
    if (code == "bighead1" || code == "bighead2" || code == "bighead3" ||
        code == "bighead4")
        return {MonsterKind::Bighead, false};
    if (code == "hellbovine")
        return {MonsterKind::HellBovine, false};
    if (code == "skmage_fire1" || code == "skmage_fire2" ||
        code == "skmage_ltng1" || code == "skmage_ltng2")
        return {MonsterKind::SkeletonMage, false};
    if (code == "fetish1")
        return {MonsterKind::Fetish, false};
    if (code == "vampire5")
        return {MonsterKind::Vampire, false};
    if (code == "fallenshaman1" || code == "fallenshaman2" ||
        code == "fallenshaman3" || code == "fallenshaman4")
        return {MonsterKind::FallenShaman, false};
    if (code == "crownest1" || code == "crownest2")
        return {MonsterKind::FoulCrowNest, false};
    if (code == "foulcrow1" || code == "foulcrow2")
        return {MonsterKind::BloodHawk, false};
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

#include "gameplay/model/definitions.hpp"
#include <stdexcept>

namespace d2x {
const MonsterDefinition &monsterDefinition(MonsterKind id) {
    static const MonsterDefinition fallen{MonsterKind::Fallen, "fa", 100, 1.9f, 6, 1.2f, 24, 1.8f};
    static const MonsterDefinition zombie{MonsterKind::Zombie, "zm", 100, 1.3f, 6, 1.2f, 24, 1.8f};
    static const MonsterDefinition skeleton{MonsterKind::Skeleton,
                                            "sk",
                                            fallen.maxLife,
                                            fallen.speed,
                                            fallen.damage,
                                            fallen.attackInterval,
                                            fallen.sightRange,
                                            fallen.attackRange};
    static const MonsterDefinition rogue{MonsterKind::CorruptRogue,
                                         "cr",
                                         fallen.maxLife,
                                         fallen.speed,
                                         fallen.damage,
                                         fallen.attackInterval,
                                         fallen.sightRange,
                                         fallen.attackRange};
    static const MonsterDefinition brute{MonsterKind::Brute, "ye", fallen.maxLife,
                                         fallen.speed, fallen.damage, fallen.attackInterval,
                                         fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition goatman{MonsterKind::Goatman, "gm", fallen.maxLife,
                                           fallen.speed, fallen.damage, fallen.attackInterval,
                                           fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition quillrat{MonsterKind::QuillRat, "si", fallen.maxLife,
                                            fallen.speed, fallen.damage, fallen.attackInterval,
                                            fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition wraith{MonsterKind::Wraith, "wr", fallen.maxLife,
                                          fallen.speed, fallen.damage, fallen.attackInterval,
                                          fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition lancer{MonsterKind::CorruptLancer, "cr", fallen.maxLife,
                                          fallen.speed, fallen.damage, fallen.attackInterval,
                                          fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition archer{MonsterKind::CorruptArcher, "cr", fallen.maxLife,
                                          fallen.speed, fallen.damage, fallen.attackInterval,
                                          fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition skeletonBow{MonsterKind::SkeletonBow, "sk", fallen.maxLife,
                                               fallen.speed, fallen.damage, fallen.attackInterval,
                                               fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition bighead{MonsterKind::Bighead, "bh", fallen.maxLife,
                                           fallen.speed, fallen.damage, fallen.attackInterval,
                                           fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition hellBovine{MonsterKind::HellBovine, "ec", fallen.maxLife,
                                               fallen.speed, fallen.damage, fallen.attackInterval,
                                               fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition skeletonMage{MonsterKind::SkeletonMage, "sk", fallen.maxLife,
                                                fallen.speed, fallen.damage, fallen.attackInterval,
                                                fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition fetish{MonsterKind::Fetish, "fe", fallen.maxLife,
                                          fallen.speed, fallen.damage, fallen.attackInterval,
                                          fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition vampire{MonsterKind::Vampire, "va", fallen.maxLife,
                                           fallen.speed, fallen.damage, fallen.attackInterval,
                                           fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition fallenShaman{MonsterKind::FallenShaman, "fs", fallen.maxLife,
                                                fallen.speed, fallen.damage, fallen.attackInterval,
                                                fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition foulCrowNest{MonsterKind::FoulCrowNest, "bn", fallen.maxLife,
                                                0, fallen.damage, fallen.attackInterval, 20, 0};
    static const MonsterDefinition bloodHawk{MonsterKind::BloodHawk, "bk", fallen.maxLife,
                                             fallen.speed, fallen.damage, fallen.attackInterval,
                                             fallen.sightRange, fallen.attackRange};
    static const MonsterDefinition arach{MonsterKind::Arach, "sp", fallen.maxLife,
                                         fallen.speed, fallen.damage, fallen.attackInterval,
                                         fallen.sightRange, fallen.attackRange};
    // Summon life, movement, damage and timing are required original content; zero prevents fallback combat.
    static const MonsterDefinition necroSkeleton{MonsterKind::NecroSkeleton, "sk", 0, 0, 0, 0, 24, 1.8f};
    static const MonsterDefinition smith{MonsterKind::Smith, "5p", 0, 0, 0, 0, 40, 0};
    static const MonsterDefinition griswold{MonsterKind::Griswold, "gz", 0, 0, 0, 0, 40, 0};
    static const MonsterDefinition bloodRaven{MonsterKind::BloodRaven, "cr", 0, 0, 0, 0, 50, 0};
    static const MonsterDefinition andariel{MonsterKind::Andariel, "an", 0, 0, 0, 0, 40, 0};
    switch (id) {
    case MonsterKind::Andariel: return andariel;
    case MonsterKind::Smith: return smith;
    case MonsterKind::Griswold: return griswold;
    case MonsterKind::BloodRaven: return bloodRaven;
    case MonsterKind::Fallen:
        return fallen;
    case MonsterKind::Zombie:
        return zombie;
    case MonsterKind::NecroSkeleton:
        return necroSkeleton;
    case MonsterKind::Skeleton:
        return skeleton;
    case MonsterKind::CorruptRogue:
        return rogue;
    case MonsterKind::Brute:
        return brute;
    case MonsterKind::Goatman:
        return goatman;
    case MonsterKind::QuillRat:
        return quillrat;
    case MonsterKind::Wraith:
        return wraith;
    case MonsterKind::CorruptLancer:
        return lancer;
    case MonsterKind::CorruptArcher:
        return archer;
    case MonsterKind::SkeletonBow:
        return skeletonBow;
    case MonsterKind::Bighead:
        return bighead;
    case MonsterKind::HellBovine:
        return hellBovine;
    case MonsterKind::SkeletonMage:
        return skeletonMage;
    case MonsterKind::Fetish:
        return fetish;
    case MonsterKind::Vampire:
        return vampire;
    case MonsterKind::FallenShaman:
        return fallenShaman;
    case MonsterKind::FoulCrowNest:
        return foulCrowNest;
    case MonsterKind::BloodHawk:
        return bloodHawk;
    case MonsterKind::Arach:
        return arach;
    case MonsterKind::Count:
        break;
    }
    throw std::out_of_range("Unknown monster definition");
}
} // namespace d2x

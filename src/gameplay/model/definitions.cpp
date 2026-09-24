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
    switch (id) {
    case MonsterKind::Fallen:
        return fallen;
    case MonsterKind::Zombie:
        return zombie;
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
    case MonsterKind::Count:
        break;
    }
    throw std::out_of_range("Unknown monster definition");
}
const PlayerRules &playerRules() {
    static const PlayerRules rules;
    return rules;
}
} // namespace d2x

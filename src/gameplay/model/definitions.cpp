#include "gameplay/model/definitions.hpp"
#include <stdexcept>

namespace d2x {
const MonsterDefinition &monsterDefinition(MonsterKind id) {
    static const MonsterDefinition fallen{MonsterKind::Fallen, "fa", 100, 1.9f, 6, 1.2f, 24, 1.8f};
    static const MonsterDefinition zombie{MonsterKind::Zombie, "zm", 100, 1.3f, 6, 1.2f, 24, 1.8f};
    switch (id) {
    case MonsterKind::Fallen:
        return fallen;
    case MonsterKind::Zombie:
        return zombie;
    }
    throw std::out_of_range("Unknown monster definition");
}
const PlayerRules &playerRules() {
    static const PlayerRules rules;
    return rules;
}
} // namespace d2x

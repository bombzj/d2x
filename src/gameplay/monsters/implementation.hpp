#pragma once
#include "gameplay/monsters/kind.hpp"
#include <string>

namespace d2x {
struct MonsterImplementation {
    MonsterKind kind;
    bool substitute;
};
// Explicit implementation registry; resource identities remain independent.
MonsterImplementation monsterImplementation(const std::string &code);
} // namespace d2x

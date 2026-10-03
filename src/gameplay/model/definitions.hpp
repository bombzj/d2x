#pragma once
#include "core/math.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/monsters/kind.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
// Native Levels.txt IDs. Template previews occupy a separate range (10000 + Def).
enum class RegionId { Encampment = 1 };
enum class Interaction { None, Talk, Heal, Travel, Stash, Loot, Shrine, Well,
                         QuestTree, QuestStone, QuestGibbet, QuestTome, QuestMalus, Door, TeleportPad, Stair, ActTwoQuest };

struct RegionDefinition {
    RegionId id;
    std::string name, mapPath;
    bool safe = false;
};
} // namespace d2x

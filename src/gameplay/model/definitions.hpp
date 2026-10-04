#pragma once
#include "core/math.hpp"
#include "world/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
enum class Interaction { None, Talk, Heal, Travel, Stash, Loot, Shrine, Well,
                         QuestTree, QuestStone, QuestGibbet, QuestTome, QuestMalus, Door, TeleportPad, Stair, ActTwoQuest, QuestObject };

} // namespace d2x

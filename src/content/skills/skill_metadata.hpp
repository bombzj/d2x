#pragma once
#include "gameplay/skills/equipment.hpp"
#include <array>
#include <string>
#include <vector>

namespace d2x {
struct SkillMetadata : SkillEquipmentRule {
    int id = -1, page = 0, row = 0, column = 0, iconCell = -1;
    int listRow = -1, listPool = 0;
    int requiredLevel = 0, maximumRank = 0;
    // Strength, dexterity, vitality, energy, matching the character display.
    std::array<int, 4> requiredAttributes{};
    std::string classCode, sourceName, name, description;
    std::vector<int> prerequisites;
    bool leftAllowed = false, passive = false, allowedInTown = false;
    bool inGame = false, aura = false;
    int startMana = 0, serverDoFunction = 0;
    // Native paired hand groups, not a per-skill weapon/shield switch.
    bool itemSkill = false; // Scroll/Book SrvDo113; availability requires native quantity.
    std::string animationMode;
    bool hasTreeNode() const {
        return page >= 1 && page <= 3 && row >= 1 && row <= 6 && column >= 1 && column <= 3;
    }
};
} // namespace d2x

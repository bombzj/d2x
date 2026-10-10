#pragma once
#include <array>
#include <string>
#include <vector>

namespace d2x {
enum class BasicSkillAction { None, Attack, Throw, LeftHandSwing, LeftHandThrow };
struct SkillMetadata {
    int id = -1, page = 0, row = 0, column = 0, iconCell = -1;
    int listRow = -1, listPool = 0;
    int requiredLevel = 0, maximumRank = 0;
    // Strength, dexterity, vitality, energy, matching the character display.
    std::array<int, 4> requiredAttributes{};
    std::string classCode, sourceName, name, description;
    std::vector<int> prerequisites;
    bool leftAllowed = false, passive = false, allowedInTown = false;
    bool itemSkill = false; // Scroll/Book SrvDo113; availability requires native quantity.
    BasicSkillAction basicAction = BasicSkillAction::None;
    std::string animationMode;
};
} // namespace d2x

#pragma once
#include "gameplay/character/attributes.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/skills/original.hpp"
#include "resources/data_table.hpp"
#include "string_table.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2x {
struct ClassSkillTree {
    std::string classCode, iconToken, backgroundToken;
    std::array<std::string, 3> pageNames;
    std::vector<int> commonSkills;
    std::optional<int> starterSkill;
};
struct SkillRecord {
    int id = -1, page = 0, row = 0, column = 0, iconCell = -1;
    int requiredLevel = 0, maximumRank = 0;
    std::string classCode, sourceName, name, description;
    std::vector<int> prerequisites;
    bool leftAllowed = false, passive = false;
    std::optional<OriginalSkillSpec> originalEffect;
    std::optional<std::pair<int, int>> manaRecoveryPerRank;
    std::optional<std::pair<int, int>> fireMasteryPerRank;
    std::optional<std::pair<int, int>> lightningMasteryPerRank;
    std::optional<std::pair<int, int>> coldPiercePerRank;
};
struct SkillCatalog {
    std::vector<ClassSkillTree> classes;
    std::map<int, SkillRecord> skills;
    const SkillRecord *find(int id) const;
    const ClassSkillTree *tree(std::string_view classCode) const;
};
SkillCatalog loadSkillCatalog(const DataTable &skills, const DataTable &descriptions,
                              const DataTable &characterStats,
                              const std::vector<CharacterDefinition> &characters,
                              const ClassicStrings &strings);
// Legacy MVP effects; Sorceress original effects take precedence when available.
std::optional<Skill> implementedSkillEffect(const SkillRecord &record);
} // namespace d2x

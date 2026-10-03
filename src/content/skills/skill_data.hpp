#pragma once
#include "gameplay/character/attributes.hpp"
#include "content/skills/skill_metadata.hpp"
#include <memory>
#include "gameplay/skills/passive.hpp"
#include "resources/data_table.hpp"
#include "content/string_table.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2x {
struct SkillSpec;
struct ClassSkillTree {
    std::string classCode, iconToken, backgroundToken;
    std::array<std::string, 3> pageNames;
    std::vector<int> commonSkills;
    std::optional<int> starterSkill;
};
// Metadata remains directly accessible for existing content consumers. The
// execution definition is immutable and owned separately, not embedded here.
struct SkillRecord : SkillMetadata {
    std::shared_ptr<const SkillSpec> spell;
    bool auraImplemented = false;
    bool auraImmediate = false;
    SkillPassiveSpec passiveContribution;
    int passiveSuppressedByState = -1;
    bool executable() const { return !passive && (auraImplemented || spell || basicAction != BasicSkillAction::None); }
    std::optional<std::pair<int, int>> manaRecoveryPerRank;
    std::optional<std::pair<int, int>> fireMasteryPerRank;
    std::optional<std::pair<int, int>> lightningMasteryPerRank;
    std::optional<std::pair<int, int>> coldPiercePerRank;
};
struct SkillCatalog {
    struct CastTiming { int frames = 0, speed = 0, actionFrame = 0; };
    std::map<std::string, CastTiming> castTimings;
    std::map<std::string, CastTiming> attackTimings;
    std::vector<ClassSkillTree> classes;
    std::map<int, SkillRecord> skills;
    const SkillRecord *find(int id) const;
    const ClassSkillTree *tree(std::string_view classCode) const;
};
SkillCatalog loadSkillCatalog(const DataTable &skills, const DataTable &descriptions,
                              const DataTable &characterStats,
                              const std::vector<CharacterDefinition> &characters,
                              const ClassicStrings &strings);
} // namespace d2x

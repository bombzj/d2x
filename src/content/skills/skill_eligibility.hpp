#pragma once
#include "skill_metadata.hpp"
#include <cstddef>
#include <map>
#include <optional>
#include <string_view>

namespace d2x {
class DataTable;
// One MPQ importer for UI metadata and protocol eligibility. No execution data.
SkillMetadata loadSkillEligibilityMetadata(const DataTable &, std::size_t row);
// Attack plus CharStats.Skill 1..10; native 0x94 may omit these actions.
std::vector<int> loadInnateSkillIds(const DataTable &skills, const DataTable &characterStats, std::size_t row);
struct SkillEligibilityInput {
    std::string_view classCode;
    std::optional<int> baseRank, effectiveRank, level, skillPoints;
    std::array<std::optional<int>, 4> attributes;
    std::map<int, int> prerequisiteRanks;
    bool innate = false, dead = false, town = false;
    // Optional display facts. Absence does not invent an equipment/mana result;
    // requests without these facts remain subject to native server validation.
    std::optional<bool> equipmentReady;
    std::optional<float> mana, requiredMana;
};
struct SkillEligibility {
    bool available = false, canAllocate = false, pickerEnabled = false, usableNow = false;
    bool canSelect(bool leftHand, const SkillMetadata &skill) const {
        return pickerEnabled && (!leftHand || skill.leftAllowed);
    }
};
// Pure read-only rules. Unknown required learning inputs cannot authorize a point.
SkillEligibility evaluateSkillEligibility(const SkillMetadata &, const SkillEligibilityInput &);
} // namespace d2x

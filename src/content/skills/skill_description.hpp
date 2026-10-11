#pragma once
#include "description_values.hpp"
#include <string>
#include <vector>

namespace d2x {
struct SkillDescriptionResult {
    std::vector<std::string> lines;
    std::string bonusHeading;
    // Program/input limitations belong to the description, not cast eligibility.
    std::vector<int> unknownPrograms;
};
SkillDescriptionResult describeSkill(const SkillDescriptionCatalog &,const SkillDescriptionInput &,
                                     SkillDescriptionContext);
std::string describeSkillCharges(const SkillDescriptionCatalog &,int current,int maximum);
} // namespace d2x

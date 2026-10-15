#pragma once
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2x {
struct PropertyInstruction {
    int function{};
    std::string stat;
    int value{};
};
struct PropertySkillLevels {
    int required{}, maximum{};
};
struct PropertyProgramInput {
    int roll{}, parameter{}, minimum{}, maximum{};
    int characterLevel{}, itemLevel{-1};
    std::optional<PropertySkillLevels> skill;
};
struct PropertyStat {
    std::string name;
    int value{}, layer{};
};
bool propertyUsesDirectRoll(int function);
std::vector<PropertyStat> evaluatePropertyProgram(std::span<const PropertyInstruction> program,
                                                const PropertyProgramInput &input);
}
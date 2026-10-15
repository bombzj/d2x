#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2x {
struct PropertyInstruction {
    int function{};
    std::string stat;
    int value{};
    bool set{};
};
struct PropertySkillLevels {
    int required{}, maximum{};
};
struct PropertyProgramInput {
    int roll{}, parameter{}, minimum{}, maximum{};
    int characterLevel{}, itemLevel{-1};
    std::optional<PropertySkillLevels> skill;
    std::span<const int32_t> operationRolls;
};
struct PropertyStat {
    std::string name;
    int value{}, layer{};
    bool set{};
};
bool propertyUsesDirectRoll(int function);
std::vector<PropertyStat> evaluatePropertyProgram(std::span<const PropertyInstruction> program,
                                                const PropertyProgramInput &input);
}
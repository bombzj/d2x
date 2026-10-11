#pragma once
#include "description_formula.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
class DataTable;
class ClassicStrings;
struct SkillDescriptionLine {
    int program{};
    std::string textA, textB;
    DescriptionFormula calculationA, calculationB;
};
struct SkillDescriptionDefinition {
    int damageProgram{}, attackProgram{};
    std::array<DescriptionFormula,2> damageCalculations;
    std::array<SkillDescriptionLine,6> levels;
    std::array<SkillDescriptionLine,4> general;
    std::array<SkillDescriptionLine,7> bonuses;
    std::array<std::string,3> missiles;
    std::string manaLabel;
};
// Compiled immutable display inputs, independent of supported execution skills.
// Includes referenced hidden skills and missiles, so a description never depends
// on whether the self-hosted server implements that skill.
struct SkillDescriptionSource {
    int id{};
    std::map<std::string,int,std::less<>> numbers;
    std::map<std::string,DescriptionFormula,std::less<>> formulas;
    std::map<std::string,std::string,std::less<>> references;
    SkillDescriptionDefinition description;
    bool hasDescription{};
    std::optional<int> number(std::string_view field) const;
    std::string_view reference(std::string_view field) const;
};
struct SkillDescriptionCatalog {
    struct Passive { int skill; std::string state; };
    std::map<int,SkillDescriptionSource> skills;
    std::map<std::string,int,std::less<>> skillIds;
    std::map<std::string,SkillDescriptionSource,std::less<>> missiles, monsters;
    std::vector<SkillDescriptionSource> monsterLevels;
    std::array<std::optional<int>,3> aiCurseDivisors;
    std::map<std::string,std::string,std::less<>> labels;
    std::map<std::string,int,std::less<>> statShifts;
    std::map<std::string,std::vector<Passive>,std::less<>> passives;
    std::string_view label(std::string_view key) const;
};
SkillDescriptionCatalog loadSkillDescriptions(const std::map<std::string,DataTable,std::less<>> &,
                                              const ClassicStrings &);
} // namespace d2x

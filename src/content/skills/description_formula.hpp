#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
// Read-only SkillDesc calculations. Neither execution state nor random streams
// belong to this evaluator. References are resolved by the description adapter.
struct DescriptionExpression;
using DescriptionFormula = std::shared_ptr<const DescriptionExpression>;
struct DescriptionReference {
    enum class Kind { Parameter, Skill, Missile, Stat, SkillAtLevel };
    Kind kind = Kind::Parameter;
    std::string name, parameter, levelParameter;
};
using DescriptionNumber = std::optional<int32_t>;
using DescriptionLookup = std::function<DescriptionNumber(const DescriptionReference &)>;
DescriptionFormula compileDescriptionFormula(std::string_view source);
DescriptionNumber evaluateDescriptionFormula(const DescriptionFormula &, const DescriptionLookup &);
} // namespace d2x

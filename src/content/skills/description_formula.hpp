#pragma once
#include "gameplay/rules/formula.hpp"
#include <vector>

namespace d2x {
// Read-only SkillDesc calculations. Neither execution state nor random streams
// belong to this evaluator. References are resolved by the description adapter.
using DescriptionExpression = RuleExpression;
using DescriptionFormula = RuleFormula;
using DescriptionReference = RuleReference;
using DescriptionNumber = RuleNumber;
using DescriptionLookup = RuleLookup;
DescriptionFormula compileDescriptionFormula(std::string_view source);
DescriptionNumber evaluateDescriptionFormula(const DescriptionFormula &, const DescriptionLookup &);
} // namespace d2x

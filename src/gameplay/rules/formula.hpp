#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace d2x {
struct RuleExpression;
using RuleFormula = std::shared_ptr<const RuleExpression>;
struct RuleReference {
    enum class Kind { Parameter, Skill, Missile, Stat, SkillAtLevel };
    Kind kind = Kind::Parameter;
    std::string name, parameter, levelParameter;
};
using RuleNumber = std::optional<int32_t>;
using RuleLookup = std::function<RuleNumber(const RuleReference &)>;
using RuleRandom = std::function<RuleNumber(int32_t,int32_t)>;
RuleFormula compileRuleFormula(std::string_view source);
RuleNumber evaluateRuleFormula(const RuleFormula &formula, const RuleLookup &lookup,const RuleRandom &random = {});
RuleNumber evaluateRuleFormula(const RuleFormula &formula,const RuleLookup &lookup,uint64_t &random);
}

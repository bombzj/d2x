#include "description_formula.hpp"

namespace d2x {
DescriptionFormula compileDescriptionFormula(std::string_view source) {
    return compileRuleFormula(source);
}
DescriptionNumber evaluateDescriptionFormula(const DescriptionFormula &formula, const DescriptionLookup &lookup) {
    return evaluateRuleFormula(formula, lookup);
}
}

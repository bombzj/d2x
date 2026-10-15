#include "stat_evaluation.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdint>
#include <utility>

namespace d2x {
ResolvedItemStat evaluateItemStat(const ItemStatDefinition &definition, int rawValue,
                                 int layer, int characterLevel) {
    int value = rawValue;
    auto nativeValue=nativeShiftLeft(rawValue,unsigned(definition.valueShift));
    std::string effect = definition.name;
    if (definition.operationBase == "level" &&
        (definition.operation == 2 || definition.operation == 4 || definition.operation == 5)) {
        nativeValue = nativeMultiply(nativeValue,std::clamp(characterLevel,1,99)) >> std::clamp(definition.operationParameter,0,30);
        value = nativeValue >> definition.operationTargetShift;
        effect = definition.operationStat;
        if (definition.operation == 5) {
            if (effect == "armorclass") effect = "item_armor_percent";
            else if (effect == "maxdamage") effect = "item_maxdamage_percent";
            else if (effect == "mindamage") effect = "item_mindamage_percent";
        }
    }
    return {definition.name, std::move(effect), value, layer, rawValue,nativeValue};
}
}

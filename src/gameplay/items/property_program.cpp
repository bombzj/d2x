#include "property_program.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace d2x {
bool propertyUsesDirectRoll(int function) {
    switch (function) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
    case 9: case 10: case 12: case 13: case 14: case 21: case 22: case 24: case 36: return true;
    default: return false;
    }
}
std::vector<PropertyStat> evaluatePropertyProgram(std::span<const PropertyInstruction> program,
                                                const PropertyProgramInput &input) {
    std::vector<PropertyStat> result;
    const auto append = [&](std::string stat, int value, int layer = 0) {
        result.push_back({std::move(stat), value, layer});
    };
    for (const auto &operation : program) {
        switch (operation.function) {
        case 1: case 2: case 3: case 4: case 8: case 13:
            append(operation.stat, input.roll); break;
        case 9: case 22: case 24: append(operation.stat, input.roll, input.parameter); break;
        case 12: append(operation.stat, input.parameter, input.roll); break;
        case 36: append(operation.stat, operation.value, input.roll); break;
        case 5: append("mindamage", input.roll); break;
        case 6: append("maxdamage", input.roll); break;
        case 7:
            append("item_mindamage_percent", input.roll);
            append("item_maxdamage_percent", input.roll); break;
        case 15: append(operation.stat, input.minimum); break;
        case 16: append(operation.stat, input.maximum); break;
        case 17: append(operation.stat, input.parameter); break;
        case 20: append("item_indesctructible", 1); break;
        case 10: append(operation.stat, input.roll, input.parameter % 3 + 8 * (input.parameter / 3)); break;
        case 21: append(operation.stat, input.roll, operation.value); break;
        case 14: append("item_numsockets", input.roll > 0 ? input.roll : std::max(1, input.parameter)); break;
        case 11: case 19: {
            if (!input.skill) throw std::runtime_error("Unprepared item property skill");
            const int sourceLevel = input.itemLevel > 0 ? input.itemLevel : input.characterLevel;
            const auto skill = *input.skill;
            int rank = input.maximum;
            if (!rank) rank = std::clamp((sourceLevel - skill.required) / 4 + 1, 1,
                skill.maximum > 0 ? skill.maximum : 20);
            else if (rank < 0) rank = std::max(1, (sourceLevel - skill.required) /
                std::max(1, -(std::max(1, 99 - skill.required) / rank)));
            int amount = input.minimum;
            if (operation.function == 11)
                append(operation.stat, amount > 0 ? amount : 5, (input.parameter << 6) | (rank & 63));
            else {
                if (amount < 0) amount = rank * -amount / 8 - amount;
                amount = amount ? std::clamp(amount, 1, 255) : 5;
                append(operation.stat, (amount << 8) | amount, (input.parameter << 6) | (rank & 63));
            }
            break;
        }
        case 0: case 23: break;
        default: throw std::runtime_error("Unsupported item property function: " + std::to_string(operation.function));
        }
    }
    return result;
}
}
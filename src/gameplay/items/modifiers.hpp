#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
// Typed copies of Properties.txt instructions. Execution belongs to the item
// rules, while the content adapter preserves each source row and slot.
struct PropertyOperation {
    int function = 0;
    std::string stat, set, value;
};
struct PropertyDefinition {
    size_t row = 0;
    std::string code;
    std::vector<PropertyOperation> operations;
};
struct ItemStatDefinition {
    size_t row = 0;
    std::string name;
    std::optional<int> id;
    int descriptionPriority = 0, descriptionFunction = 0, descriptionValue = 0;
    std::string positive, negative, suffix;
    int operation = 0, operationParameter = 0;
    std::string operationBase, operationStat;
};
struct ResolvedItemStat {
    std::string name, effect;
    int value = 0, layer = 0, rawValue = 0;
};
} // namespace d2x

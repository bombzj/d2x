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
};
} // namespace d2x

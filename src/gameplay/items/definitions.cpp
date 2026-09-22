#include "definitions.hpp"
#include <stdexcept>

namespace d2x {
ItemCatalog::ItemCatalog(std::vector<ItemDefinition> definitions) {
    for (auto &definition : definitions) {
        if (definition.code.empty() || definition.name.empty() || definition.width < 1 ||
            definition.height < 1 || definition.width > 64 || definition.height > 64 ||
            definition.maxStack < 1)
            throw std::invalid_argument("Invalid item definition: " + definition.code);
        auto code = definition.code;
        if (!entries_.emplace(std::move(code), std::move(definition)).second)
            throw std::invalid_argument("Duplicate item definition");
    }
}
const ItemDefinition *ItemCatalog::find(std::string_view code) const {
    auto found = entries_.find(code);
    return found == entries_.end() ? nullptr : &found->second;
}
} // namespace d2x

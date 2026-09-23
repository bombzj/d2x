#include "character_attributes.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace d2x {
std::vector<CharacterDefinition> loadCharacterDefinitions(const DataTable &characters) {
    // Class identifiers and sprite tokens are engine resource identities, not progression values.
    constexpr struct { const char *name, *code, *appearance; } classes[] = {
        {"Amazon", "ama", "am"}, {"Sorceress", "sor", "so"},
        {"Necromancer", "nec", "ne"}, {"Paladin", "pal", "pa"},
        {"Barbarian", "bar", "ba"}, {"Druid", "dru", "dz"},
        {"Assassin", "ass", "ai"}};
    std::vector<CharacterDefinition> definitions;
    for (size_t row = 0; row < characters.rows().size(); ++row) {
        auto name = characters.value(row, "class");
        auto identity = std::find_if(std::begin(classes), std::end(classes),
                                     [name](const auto &entry) { return name == entry.name; });
        if (identity == std::end(classes)) continue;
        auto required = [&](const char *field) {
            auto number = characters.number(row, field);
            if (!number || *number < 0)
                throw std::runtime_error(std::string("Invalid CharStats field: ") + field + " for " + std::string(name));
            return *number;
        };
        auto signedField = [&](const char *field) {
            auto number = characters.number(row, field);
            if (!number)
                throw std::runtime_error(std::string("Missing CharStats field: ") + field + " for " + std::string(name));
            return *number;
        };
        CharacterDefinition result;
        result.name = name; result.code = identity->code; result.appearance = identity->appearance;
        result.sourceRow = row;
        result.strength = required("str"); result.dexterity = required("dex");
        result.vitality = required("vit"); result.energy = required("int");
        result.stamina = required("stamina"); result.lifeAdd = required("hpadd");
        result.lifePerLevel = required("LifePerLevel");
        result.manaPerLevel = required("ManaPerLevel");
        result.staminaPerLevel = required("StaminaPerLevel");
        result.lifePerVitality = required("LifePerVitality");
        result.manaPerEnergy = required("ManaPerMagic");
        result.staminaPerVitality = required("StaminaPerVitality");
        result.statPerLevel = required("StatPerLevel");
        result.manaRegen = required("ManaRegen");
        result.toHitFactor = signedField("ToHitFactor");
        result.blockFactor = required("BlockFactor");
        if (!result.statPerLevel || !result.manaRegen)
            throw std::runtime_error("MPQ character progression data is incomplete: " + result.name);
        if (std::any_of(definitions.begin(), definitions.end(),
                        [&](const auto &entry) { return entry.name == result.name; }))
            throw std::runtime_error("Duplicate MPQ character definition: " + result.name);
        definitions.push_back(std::move(result));
    }
    if (definitions.empty() ||
        std::none_of(definitions.begin(), definitions.end(),
                     [](const auto &entry) { return entry.code == "bar"; }))
        throw std::runtime_error("MPQ CharStats lacks the default character definition");
    return definitions;
}
} // namespace d2x

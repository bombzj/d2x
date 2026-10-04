#pragma once
#include "gameplay/items/state.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/loot/affix.hpp"
#include <optional>

namespace d2x {
struct CubeBaseRecord {
    std::string normal, exceptional, elite;
    unsigned minimumStack = 1, spawnStack = 1;
};
struct CubeInput {
    std::string code;
    bool type = false, any = false, upgraded = false;
    bool noSockets = false, sockets = false, noEthereal = false, ethereal = false, noRuneword = false;
    int tier = 0, specialRow = -1;
    unsigned quantity = 1;
    std::optional<ItemQuality> quality;
};
enum class CubeOutputKind { Code, Type, UseItem, UseType, CowPortal, UberPortal, UberFinale };
struct CubeProperty { PropertyRange property; int chance = 0; };
struct CubeOutput {
    CubeOutputKind kind = CubeOutputKind::Code;
    std::string code;
    std::optional<ItemQuality> quality;
    bool copy = false, unsocket = false, repair = false, recharge = false, ethereal = false;
    int tier = 0, socketCount = 0, prefix = 0, suffix = 0;
    unsigned quantity = 0;
    int level = 0, playerPercent = 0, itemPercent = 0;
    std::vector<CubeProperty> properties;
};
struct CubeRecipe {
    size_t row = 0;
    int version = 0, minimumDifficulty = 0, operation = 0;
    bool ladder = false;
    std::string characterClass;
    unsigned inputCount = 0;
    std::vector<CubeInput> inputs;
    std::vector<CubeOutput> outputs;
};
bool matchesCubeInput(const CubeInput &, const ItemInstance &, const ItemDefinition &, const CubeBaseRecord &);
} // namespace d2x

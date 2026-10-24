#pragma once
#include <optional>
#include <vector>
#include <utility>
namespace d2x {
struct ClassicData;struct CubeRecipe;struct ItemInstance;
struct Pd2CubeInputs {
    std::vector<const ItemInstance *> ordered;
    std::vector<std::pair<const ItemInstance *,unsigned>> consumed;
};
std::optional<Pd2CubeInputs> matchPd2CubeInputs(const ClassicData &,const CubeRecipe &,
    const std::vector<const ItemInstance *> &,int level,int difficulty);
}

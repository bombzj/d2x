#pragma once
#include <functional>
#include <cstdint>
namespace d2x {
struct ClassicData;
struct CubeRecipe;
namespace server::crafting {struct Prepared;}
using Pd2CubeStep=std::function<const CubeRecipe *(server::crafting::Prepared &,uint64_t &,bool)>;
void preparePd2Cube(const ClassicData &,server::crafting::Prepared &,const Pd2CubeStep &);
}

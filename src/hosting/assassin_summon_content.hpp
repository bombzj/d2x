#pragma once
#include "server/systems/companions/preparation.hpp"
namespace d2x {
struct ClassicData;class Archives;
server::companions::Prepared prepareAssassinSummon(const server::companions::Preparation &,Archives &,const ClassicData &);
}

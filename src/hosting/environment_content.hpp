#pragma once
#include "combat_content.hpp"
namespace d2x {
std::optional<server::PreparedMonster> prepareEnvironmentMonster(Archives &,const ClassicData &,AreaGenerationRequest,MonsterIdentity,Vec);
void prepareEnvironmentPopulation(Archives &,const ClassicData &,PreparedWorldArea &);
}

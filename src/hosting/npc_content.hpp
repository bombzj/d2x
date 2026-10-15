#pragma once
#include "game_content.hpp"
namespace d2x {
struct MonsterRecord;
std::shared_ptr<const server::MonsterRule> prepareNeutralMonsterRule(Archives &,const ClassicData &,AreaGenerationRequest,const MonsterRecord &);
void prepareNpcs(Archives &, const ClassicData &, PreparedWorldArea &);
}

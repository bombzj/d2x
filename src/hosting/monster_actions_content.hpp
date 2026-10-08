#pragma once
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "server/runtime/combat_rules.hpp"
namespace d2x {
struct ClassicData;
std::optional<server::MonsterMissileRule> prepareMonsterMissile(const ClassicData &, int definition, int rank);
std::map<uint8_t,server::MonsterAttackRule> prepareMonsterAttacks(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,const MonsterCombatProfile &,int difficulty);
bool prepareMonsterSpecialActions(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,server::MonsterRule &);
bool prepareMonsterComponents(const ClassicData &,const MonsterRecord &,server::MonsterRule &);
bool prepareMonsterLifecycle(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,server::MonsterRule &);
}

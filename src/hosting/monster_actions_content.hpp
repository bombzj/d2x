#pragma once
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "server/runtime/combat_rules.hpp"
namespace d2x {
struct ClassicData;
bool prepareMonsterActionClock(Archives &,const AnimDataTable &,const DataTable &,const MonsterRecord &,std::string_view,int event,server::MonsterAttackRule &);
void prepareMonsterStateRules(const ClassicData &, size_t monsterRow, server::MonsterRule &);
void prepareMonsterBirthProperties(const ClassicData &,size_t monsterRow,server::MonsterRule &);
void prepareMonsterPlacementRules(const ClassicData &,size_t monsterRow,server::MonsterRule &);
void prepareMonsterSkillRanks(const ClassicData &,size_t monsterRow,server::MonsterRule &);
std::optional<server::MonsterMissileRule> prepareMonsterMissile(const ClassicData &, int definition, int rank);
std::map<uint8_t,server::MonsterAttackRule> prepareMonsterAttacks(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,const MonsterCombatProfile &,int difficulty,bool ladderStats=true);
bool prepareMonsterSpecialActions(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,server::MonsterRule &);
bool hasAct2MonsterSkills(std::string_view ai);
bool prepareAct2MonsterSkills(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,server::MonsterRule &);
bool prepareMonsterComponents(const ClassicData &,const MonsterRecord &,server::MonsterRule &);
bool prepareMonsterLifecycle(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,server::MonsterRule &);
}

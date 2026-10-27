#pragma once
#include <cstddef>
#include <string_view>
#include <string>
namespace d2x {struct ClassicData;struct MonsterRecord;class Archives;class AnimDataTable;
namespace server {struct MonsterRule;struct MonsterAttackRule;struct SkillRules;}
namespace hosting::pd2 {
void prepareMonsterSkillPrograms(const ClassicData &,server::SkillRules &);
void prepareMonsterAi(const ClassicData &,const MonsterRecord &,server::MonsterRule &);
bool prepareMonsterActions(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,server::MonsterRule &,std::string *reason=nullptr);
bool sceneController(const MonsterRecord &);
bool hasAttackMode(const ClassicData &,const MonsterRecord &,int slot);
bool usesSecondaryAttack(const MonsterRecord &);
bool hasMode(const ClassicData &,const MonsterRecord &,std::string_view mode);
bool prepareNativeClock(Archives &,const AnimDataTable &,const MonsterRecord &,std::string_view mode,server::MonsterAttackRule &,bool skillAction=false);
}}

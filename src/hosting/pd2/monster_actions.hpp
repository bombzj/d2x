#pragma once
#include <cstddef>
namespace d2x {struct ClassicData;struct MonsterRecord;class Archives;class AnimDataTable;
namespace server {struct MonsterRule;struct SkillRules;}
namespace hosting::pd2 {
void prepareMonsterSkillPrograms(const ClassicData &,server::SkillRules &);
void prepareMonsterAi(const ClassicData &,const MonsterRecord &,server::MonsterRule &);
bool prepareMonsterActions(Archives &,const ClassicData &,const AnimDataTable &,const MonsterRecord &,server::MonsterRule &);
bool sceneController(const MonsterRecord &);
bool hasAttackMode(const ClassicData &,const MonsterRecord &,int slot);
}}

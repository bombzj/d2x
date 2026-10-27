#pragma once
#include "hosting/game_content.hpp"
#include "gameplay/monsters/region_generation.hpp"
namespace d2x {
std::shared_ptr<const MonsterRegionRules> prepareMonsterRegionRules(Archives &,const ClassicData &);
std::optional<server::PreparedMonster> prepareCombatMonster(Archives &, const ClassicData &, AreaGenerationRequest, MonsterIdentity, Vec,std::string *deferredReason=nullptr);
std::optional<server::PreparedMonster> prepareSelectedMonster(Archives &,const ClassicData &,AreaGenerationRequest,
	MonsterIdentity,int level,const MonsterEnchantment &,const MonsterEnchantment *owner=nullptr,std::string *deferredReason=nullptr);
void prepareCombatPopulation(Archives &, const ClassicData &, PreparedWorldArea &);
void prepareQuestWorld(Archives &,const ClassicData &,PreparedWorldArea &);
}

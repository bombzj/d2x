#pragma once
#include "hosting/game_content.hpp"
#include "gameplay/monsters/region_generation.hpp"
namespace d2x {
std::shared_ptr<const MonsterRegionRules> prepareMonsterRegionRules(Archives &,const ClassicData &);
std::shared_ptr<const server::MeleeRules> prepareMeleeRules(const ClassicData &, const CharacterDefinition &);
std::optional<server::PreparedMonster> prepareCombatMonster(Archives &, const ClassicData &, AreaGenerationRequest, MonsterIdentity, Vec);
std::optional<server::PreparedMonster> prepareSelectedMonster(Archives &,const ClassicData &,AreaGenerationRequest,
	MonsterIdentity,int level,const MonsterEnchantment &,const MonsterEnchantment *owner=nullptr);
std::vector<server::PreparedMonster> prepareObjectParty(Archives &,const ClassicData &,AreaGenerationRequest,std::string);
void prepareCombatPopulation(Archives &, const ClassicData &, PreparedWorldArea &);
void prepareQuestWorld(Archives &,const ClassicData &,PreparedWorldArea &);
}

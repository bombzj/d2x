#pragma once
#include "hosting/game_content.hpp"
namespace d2x {
std::shared_ptr<const server::MeleeRules> prepareMeleeRules(const ClassicData &, const CharacterDefinition &);
std::optional<server::PreparedMonster> prepareCombatMonster(Archives &, const ClassicData &, AreaGenerationRequest, MonsterIdentity, Vec);
void prepareCombatPopulation(Archives &, const ClassicData &, PreparedWorldArea &);
void prepareQuestWorld(Archives &,const ClassicData &,PreparedWorldArea &);
}

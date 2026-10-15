#pragma once
#include "gameplay/skills/use_source.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
namespace d2x {
struct SkillCastSpec;
struct ChargedSkill;
struct CharacterRecord;
namespace server { struct PlayerState; }
namespace server::monsters {struct Actor;}
namespace server::attributes {struct Totals;}
}
namespace d2x::server::skills {
SkillFormulaInput formulaInput(const PlayerState &,RuleRandom random = {},std::optional<EntityId> weapon = {});
SkillFormulaInput formulaInput(const PlayerState &,const CharacterRecord &,const attributes::Totals &,
    RuleRandom random={},std::optional<EntityId> weapon={});
SkillFormulaInput formulaInput(const monsters::Actor &,StatSnapshot sampled,RuleRandom random = {});
int mastery(const PlayerState &, const std::map<int, std::pair<int, int>> &);
uint64_t periodicTick(const PlayerState &, int skill, int rank, uint64_t tick);
uint64_t periodicDuration(const PlayerState &, int skill, int rank, uint64_t tick);
SkillCastSpec evaluate(const PlayerState &, int skill, int rank);
std::optional<ChargedSkill> chargedSource(const PlayerState &, int skill, bool right);
std::optional<SkillUseSource> selectedSource(const PlayerState &, int skill, bool right);
int coldPierce(const PlayerState &);
}

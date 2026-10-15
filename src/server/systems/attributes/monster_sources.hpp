#pragma once
#include "gameplay/combat/stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/items/skill_sources.hpp"
#include <span>
#include <optional>
#include <functional>
#include <cstdint>
#include <memory>
namespace d2x {struct SummonCastSpec;struct SkillAuraChange;class CombatEffectSet;}
namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::attributes {
class MonsterResourceTrace;
std::vector<ChargedSkill> monsterChargedSkills(const monsters::Actor &,const StatList &);
using MonsterAuraMutation=std::function<void(monsters::Actor &,MonsterResourceTrace &,const SkillAuraChange &)>;
void recordMonsterItemAura(monsters::Actor &,StatValue,EntityId,const StatSnapshot &,
    std::shared_ptr<const StatList>,Fixed8 life);
// Native MONSTERS_Initialize admission values. Attack-mode fields are installed
// by their mode callback, not guessed from the default A1 content definition.
NativeUnitStats prepareMonsterStatSources(monsters::Actor &,std::optional<StatTimeContext> time={});
NativeUnitStats withUnitModeStats(const NativeUnitStats &,const StatRuleCatalog &,std::shared_ptr<const StatList>);
NativeUnitStats prepareSummonStatSources(monsters::Actor &,const SummonCastSpec &,std::span<const std::shared_ptr<const StatList>> equipment,std::optional<StatTimeContext> time={});
NativeUnitStats prepareSummonStatSources(monsters::Actor &,const SummonCastSpec &,std::span<const std::shared_ptr<const StatList>>,
    std::optional<StatTimeContext>,uint64_t tick,const MonsterAuraMutation &);
void dispatchMonsterItemAura(monsters::Actor &,MonsterResourceTrace &,StatValue,EntityId,const StatSnapshot &,
    std::shared_ptr<const StatList>,const MonsterAuraMutation &);
void assignMonsterSkill(monsters::Actor &,const CombatEffectSet &,int skill,int rank,uint64_t tick,const MonsterAuraMutation &);
void refreshMonsterStatSources(monsters::Actor &,std::shared_ptr<const StatList>,const CombatEffectSet &,
    uint64_t tick,const MonsterAuraMutation &);
NativeUnitStats prepareReviveStatSources(monsters::Actor &,const SummonCastSpec &,uint64_t tick,std::span<const ModifierStatSource>);
}

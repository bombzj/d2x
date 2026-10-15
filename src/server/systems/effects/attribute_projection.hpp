#pragma once
#include <cstdint>
#include <cstddef>
namespace d2x {class CombatEffectSet;namespace server {struct TransientAttributes;}}
namespace d2x {struct EffectApplication;struct SkillCastSpec;struct CombatEffectSpec;struct StatRuleCatalog;}
namespace d2x {struct CurseSpec;struct StatValue;struct AuraDefinition;}
#include <span>
#include <functional>
#include <utility>
#include "gameplay/effects/definition.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/units/resources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
namespace d2x::server {struct PlayerState;}
namespace d2x::server::monsters {struct Actor;struct LifeChange;}
namespace d2x::server::effects {
struct PlayerPoisonCandidate;
bool shortenAuraHarmfulDurations(CombatEffectSet &,std::optional<PoisonStatus> &,uint64_t tick,int percentage,bool intrinsicPoison=false);
TransientAttributes projectEffectAttributes(const CombatEffectSet &,uint64_t tick,bool includePendingRemoval=false);
EffectApplication applySkillState(CombatEffectSet &,const SkillCastSpec &,const CombatEffectSpec &,const StatRuleCatalog &,const CombatStateRules &,uint64_t tick,const std::function<void()> &observe={});
EffectApplication applyNativeAuraState(CombatEffectSet &,const CombatEffectSpec &,
    const std::optional<std::vector<StatValue>> &,size_t callbackWrites,const StatRuleCatalog &,uint64_t tick,
    const std::function<void()> &observe,const std::function<std::optional<bool>(StatValue)> &procedure={},bool *affected=nullptr);
class PlayerResourceTrace {
    const PlayerState &player_;
    PersistentCharacter current_;
    attributes::Totals previous_;
    uint64_t tick_{};
    std::optional<StatTimeContext> time_;
    std::vector<ResourceMaximums> maximums_;
    uint64_t passiveRefresh_{};
    uint64_t staminaRestore_{};
  public:
    PlayerResourceTrace(const PlayerState &,uint64_t tick,Fixed8 manaCost={},std::optional<StatTimeContext> time={});
    void sample(const CombatEffectSet &,bool includePendingRemoval=false);
    void withdrawPassiveState(int state,const CombatEffectSet &);
    void seedResources(Fixed8 life,Fixed8 mana,Fixed8 stamina);
    void inheritPrepared(CharacterRecord,attributes::Totals,const CombatEffectSet &);
    const CharacterRecord &record() const {return current_.player;}
    const attributes::Totals &totals() const {return previous_;}
    ResourceMaximums maximum() const;
    const std::vector<ResourceMaximums> &maximums() const {return maximums_;}
};
EffectApplication applyCurseState(CombatEffectSet &,const CurseSpec &,const CombatEffectSpec &,std::span<const StatValue>,const StatRuleCatalog &,uint64_t tick,
    const std::function<void()> &observe={},const std::function<void(const EffectApplication &)> &afterPost={});
using MonsterResourceTrace=attributes::MonsterResourceTrace;
void prepareUnitControl(const monsters::Actor &,const CombatEffectSet &before,const CombatEffectSet &after,monsters::LifeChange &,uint64_t tick);
void retainPreparedUnitControl(monsters::LifeChange &,const monsters::LifeChange &);
void applyImmediateAuraOwner(CombatEffectSet &,const ActorContext &,const AuraDefinition &,const SkillAuraChange &,
    const StatRuleCatalog &,int noManaState,int passiveState,const attributes::AuraSourceObserver &,const attributes::AuraSourceReader &,std::vector<DomainFact> &,PlayerPoisonCandidate &);
void applyImmediateAuraTarget(CombatEffectSet &,const ActorContext &caster,const ActorContext &recipient,const AuraDefinition &,const SkillAuraChange &,
    const StatRuleCatalog &,int passiveState,const attributes::AuraSourceObserver &,const attributes::AuraSourceReader &,std::vector<DomainFact> &,PlayerPoisonCandidate &);
}

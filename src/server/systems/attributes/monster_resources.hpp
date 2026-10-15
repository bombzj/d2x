#pragma once
#include "server/systems/monsters/life_change.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include <cstdint>
#include <span>
#include <string_view>
#include <memory>
#include <functional>
#include <utility>
#include <vector>
#include <set>
namespace d2x {class CombatEffectSet;}
namespace d2x {struct SkillStatRefresh;}
namespace d2x::server::monsters {struct Actor;}
namespace d2x::server::attributes {
using MonsterSourceWrite=std::function<void(StatValue,EntityId,const StatSnapshot &,std::shared_ptr<const StatList>)>;
using MonsterStateSourceRead=std::function<std::vector<ModifierStatSource>()>;
// The attributes domain prepares sources and callback results. The monster
// domain alone commits the life and intrinsic source candidate.
class MonsterResourceTrace {
    const monsters::Actor &actor_;
    uint64_t tick_{};
    monsters::LifeChange change_;
    uint64_t passiveRefresh_{};
    bool passiveSeeded_{};
    StatSnapshot *activeFull_{}; // Synchronous nested native propagation only.
    MonsterSourceWrite sourceWritten_;
    MonsterStateSourceRead stateSources_;
    std::vector<ModifierStatSource> sampledStates_;
    void propagateSource(std::shared_ptr<const StatList> previous,std::shared_ptr<const StatList> next,EntityId,
        std::span<const ModifierStatSource>);
    void refreshPassives(const CombatEffectSet &,std::span<const ModifierStatSource>);
    void refreshPassiveSkill(int,const std::set<int> &,std::span<const ModifierStatSource>);
    void sampleMaximums();
    void storeFull(const StatSnapshot &);
  public:
    MonsterResourceTrace(const monsters::Actor &,uint64_t tick,const CombatEffectSet *baseline=nullptr);
    void observeSourceWrites(MonsterSourceWrite observer) {sourceWritten_=std::move(observer);}
    void readStateSources(MonsterStateSourceRead reader) {stateSources_=std::move(reader);}
    void inheritPassiveCache(uint64_t epoch) {if(!passiveSeeded_) {passiveRefresh_=epoch;passiveSeeded_=true;}}
    void inheritStateCallbacks(const CombatEffectSet &);
    void sample(const CombatEffectSet &,bool includePendingRemoval=false);
    void sampleSources(std::span<const ModifierStatSource>);
    StatSnapshot values(std::span<const ModifierStatSource>) const;
    void writeBaseStat(std::string_view name,int32_t rawValue,std::span<const ModifierStatSource>);
    void addBaseStat(std::string_view name,int32_t rawValue,std::span<const ModifierStatSource>);
    void refreshIntrinsic(std::span<const ModifierStatSource>);
    void postIntrinsicSource(std::shared_ptr<const StatList>,std::span<const ModifierStatSource>);
    void replaceIntrinsicSource(std::shared_ptr<const StatList>,std::span<const ModifierStatSource>);
    void withdrawIntrinsicSource(StatSourceKind,int definition,std::span<const ModifierStatSource>);
    void withdrawIntrinsicSource(StatSource,std::span<const ModifierStatSource>);
    void withdrawPassiveState(int,const CombatEffectSet &);
    void refreshItemSkills(monsters::Actor &,const SkillStatRefresh &,int32_t value);
    void seedLife(Fixed8);
    void seedShatter(bool);
    void seedPoison(std::optional<PoisonStatus>);
    void seedItemAuraTimers(std::vector<ItemAuraTimer>);
    void seedCurseControl(monsters::CurseControl,uint64_t callbackEpoch);
    const monsters::LifeChange &read() const {return change_;}
    const monsters::Actor &actor() const {return actor_;}
    monsters::LifeChange take() {return std::move(change_);}
};
}

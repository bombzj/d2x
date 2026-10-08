#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/monsters/components.hpp"
#include "gameplay/monsters/enchantment_damage.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/hydra_spec.hpp"
#include "gameplay/combat/poison.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/character/persistent_character.hpp"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::monsters {
// Sole owner of monster/NPC/companion actor runtime; content identity is preserved.
struct Admission { MonsterIdentity identity; std::optional<MonsterKind> implementation; RegionId area; Vec position; bool hostile{}; std::optional<MonsterRule> rule; std::vector<Vec> skillPositions{}; };
struct MoveRequest {
    EntityId actor; PointTarget destination; EntityId target;
    int stopDistance{}, velocityPercent{75}; bool running{};
};
struct Actor {
    EntityId id; MonsterIdentity identity; std::optional<MonsterKind> implementation;
    RegionId area; Vec position; uint64_t revision{}; std::optional<PlayerId> owner;
    MonsterRule rule;
    std::shared_ptr<const AmazonPetSpec> amazonPet;
    UnitCombatStats petStats;
    std::optional<WeaponDamage> petWeapon;
    std::shared_ptr<const PersistentCharacter> equipment;
    int64_t life{}, maximumLife{};
    std::deque<Vec> route;
    uint64_t busyUntil{}, deathTick{}, deathOccurrence{};
    EntityId killer, movementTarget;
    Vec movementGoal;
    uint64_t hitOccurrence{};
    uint64_t combatRandom{};
    MonsterEnchantmentDamageState enchantmentDamage;
    uint64_t interruption{};
    uint8_t reactionMode{};
    uint64_t reactionUntil{};
    std::array<uint8_t,16> components{};
    bool shield{};
    int stopDistance{}, velocityPercent{75};
    int effectVelocity{};
    bool running{};
    bool rewardComplete{}, moving{};
    uint64_t riseUntil{};
    uint8_t riseMode{9};
    bool corpseUnavailable{};
    uint64_t webUntil{};
    int nestSpawned{};
    struct Slow {int state{-1},percent{};uint64_t until{};};
    std::optional<Slow> slowed;
    Vec webOrigin;
    uint64_t webRandom{};
    Vec home;
    std::vector<Vec> skillPositions;
    uint64_t damageOccurrence{};
    bool lightningReady{};
    uint64_t chilledUntil{}, frozenUntil{}, nextHitTick{};
    uint64_t knockedUntil{};
    Vec knockbackSource;
    std::optional<Vec> knockbackGoal;
    std::optional<PoisonStatus> poison;
};
struct State {
    std::map<EntityId, Actor> actors;
    // Scoped to the actual instance/area/class, including dynamically born units.
    std::map<RegionId, std::map<int, MonsterComponentPalette>> componentPalettes;
    struct DeathCascade {EntityId source;uint64_t due{};};
    std::map<EntityId,DeathCascade> deathCascades;
};
struct Ports { const AreaStore &areas; const PlayerStore &players; EntityIds &ids; uint64_t &random; EventOutbox &events; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    const Actor *find(EntityId id) const { auto it = state_.actors.find(id); return it == state_.actors.end() ? nullptr : &it->second; }
    DomainResult<> beginAttack(EntityId, uint64_t until);
    bool resurrectionTarget(EntityId source,EntityId corpse,uint64_t tick) const;
    DomainResult<> resurrect(EntityId source,EntityId corpse,uint64_t tick);
    DomainResult<EntityId> spawnNestChild(EntityId,uint64_t tick);
    DomainResult<EntityId> spawnNestChild(EntityId,uint64_t tick,Vec origin);
    DomainResult<> teleport(EntityId,Vec,uint64_t tick,int heal = 0);
    DomainResult<> activateWeb(EntityId,uint64_t tick);
    DomainResult<> heal(EntityId,int64_t amount);
    DomainResult<> slow(EntityId,int state,int percent,uint64_t frames,uint64_t tick);
    DomainResult<> damage(EntityId, EntityId source, int64_t amount, uint64_t tick, uint64_t coldFrames = 0, bool freeze = false, uint8_t hitClass = 0, std::optional<PoisonApplication> poison = {},bool poisonOnly=false);
    DomainResult<> block(EntityId,uint64_t tick);
    DomainResult<> lightningEmission(EntityId,bool emitted,uint64_t tick);
    void rewardComplete(EntityId);
    void commitCombatRandom(EntityId id, uint64_t value) { if (auto it=state_.actors.find(id); it!=state_.actors.end()) it->second.combatRandom=value; }
    void commitEnchantmentDamage(EntityId id,MonsterEnchantmentDamageState value) { if(auto it=state_.actors.find(id);it!=state_.actors.end()) it->second.enchantmentDamage=std::move(value); }
    DomainResult<> introduction(EntityId,uint64_t tick);
    void velocityModifier(EntityId id,int percent) { if(auto it=state_.actors.find(id);it!=state_.actors.end()) {if(it->second.effectVelocity!=percent) {it->second.effectVelocity=percent;++it->second.revision;}} }
    DomainResult<EntityId> admit(const Admission &);
    DomainResult<> requestMove(const MoveRequest &);
    void stop(EntityId);
    void hitDelay(EntityId id, uint64_t until);
    void knockback(EntityId, Vec source, uint64_t tick);
    DomainResult<> remove(EntityId);
    DomainResult<std::map<EntityId,Actor>> prepareHydra(const ActorContext &, const HydraSpec &, Vec) const;
    DomainResult<std::map<EntityId,Actor>> prepareAmazon(const ActorContext &,const MonsterRule &,Vec,const SummonCastSpec &,std::shared_ptr<PersistentCharacter>,size_t,std::optional<WeaponDamage> = {}) const;
    void commitAmazon(std::map<EntityId,Actor> &&,size_t) noexcept;
    void commitHydra(std::map<EntityId,Actor> &&) noexcept;
    void retire(EntityId, uint64_t tick);
    DomainResult<> warpPet(EntityId,const ActorContext &,Vec);
    std::optional<std::pair<Vec,int>> targetPosition(EntityId,RegionId) const;
    StepStatus step(TickContext, FrameFacts &);
};
}

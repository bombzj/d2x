#pragma once
#include "core/id.hpp"
#include "admission.hpp"
#include "gameplay/monsters/corpse.hpp"
#include "curse_control.hpp"
#include "gameplay/events/frame_queue.hpp"
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/monsters/components.hpp"
#include "gameplay/monsters/enchantment_damage.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/hydra_spec.hpp"
#include "gameplay/skills/conversion_spec.hpp"
#include "gameplay/combat/poison.hpp"
#include "gameplay/combat/alignment.hpp"
#include "gameplay/combat/open_wounds.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/units/restoration.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/stat_callbacks.hpp"
#include "gameplay/skills/use_source.hpp"
#include "gameplay/combat/stats.hpp"
#include "life_change.hpp"
#include "damage_status.hpp"
#include <span>
#include <stdexcept>
#include "world/native_path_step.hpp"
#include <deque>
#include <map>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <functional>
namespace d2x::server::effects {struct AuraUnitPublication;}

namespace d2x::server {struct HydraRules;struct SkillRules;struct RoomUnitRegistration;struct NpcRule;struct RoomView;struct RoomRandomLoan;}
namespace d2x::server {struct RoomView;}
namespace d2x::server::monsters {
class NativeBirthPublication;
// Sole owner of monster/NPC/companion actor runtime; content identity is preserved.
struct MoveRequest {
    EntityId actor; PointTarget destination; EntityId target;
    int stopDistance{}, velocityPercent{75}; bool running{};
    int pathSteps{}; // Nonzero stores native unit arrival distance + 1.
    int circleTurn{},circleDistance{};
    uint8_t pathType{13},pathDistance{5};
};
struct Actor {
    std::shared_ptr<RoomUnitRegistration> roomAdmission;
    std::shared_ptr<FrameEventQueue::PreparedInsertion> initialAiTimer;
    std::shared_ptr<FrameEventQueue::PreparedInsertion> initialModeTimer,initialWakeTimer;
    std::vector<std::shared_ptr<FrameEventQueue::PreparedInsertion>> initialModifierTimers;
    std::optional<uint64_t> modeFrame;
    EntityId id; MonsterIdentity identity; std::optional<MonsterKind> implementation;
    RegionId area; Vec position; uint64_t revision{}; std::optional<PlayerId> owner;
    bool hostile{};
    bool restoredFromInactive{};
    bool uncounted{};
    uint64_t admissionRevision{1};
    uint64_t admissionTick{};
    size_t birthEquipmentItems{};
    std::optional<std::set<size_t>> birthUniquesExpected;
    std::set<size_t> birthLimitedUniques;
    std::optional<uint8_t> ravenInitialCircle;
    bool questNpc{}; // Quest/service capability; never a substitute hostile AI.
    uint64_t allegianceRevision{};
    MonsterRule rule;
    std::shared_ptr<const MonsterRule> admissionRule;
    std::optional<NativeCollisionPattern> collisionPattern;
    std::shared_ptr<const AmazonPetSpec> amazonPet;
    std::shared_ptr<const AssassinPetSpec> assassinPet;
    std::shared_ptr<const DruidPetSpec> druidPet;
    bool necroSummon{};
    bool revived() const {return necroPet && necroPet->kind==NecroSummonKind::Revive;}
    std::shared_ptr<const NecroPetSpec> necroPet;
    std::optional<ItemInstance> necroMaterial;
    using CurseControl=d2x::server::monsters::CurseControl;
    CurseControl curseControl;
    EntityId attractionTarget;uint64_t attractionUntil{};
    std::vector<AiResetTimer> aiResetTimers;
    bool nativeAiResetTracked{};
    uint64_t specialTargetOrder{}; // Group 9 head insertion; zero means no node.
    bool detachedMinion{}; // Runtime OwnerGUIDEx; spawn identity stays intact.
    uint64_t aiFunctionRevision{};
    std::optional<NativeSkillSlot> rightSkill; // Native right selection, separate from used/action skill.
    uint64_t aiThinkAt{};
    uint64_t aiThinkRevision{};
    EntityId tauntTarget;
    bool mainOwnerCleared{};
    // AIGENERAL owner/list are distinct from UNITS_StoreOwner and PlayerPets.
    // Present zero explicitly clears AI ownership; absent retains preset rules.
    std::optional<EntityId> nativeMinionOwner;
    std::vector<EntityId> nativeMinions;
    bool ownsNativeMinions{},transferMinionsOnDeath{},ownershipDeathHandled{};
    bool killMinionsOnDeath{},minionDeathHandled{};
    bool hireling{};
    uint64_t petRegistrationOrder{}; // PlayerPets appends to each type's list.
    bool inactivePet{}; // PlayerPets nFlags & 1; kept independently of GUID.
    struct Conversion {
        int state{-1},alignment{-1},stat{-1};uint64_t until{};
        std::optional<ConversionResourceSave> savedResources;
        int savedState{-1};
        NativeConversionProcedure procedure{NativeConversionProcedure::Paladin};
    };
    std::optional<Conversion> conversion;
    uint64_t hirelingInventoryRevision{}, hirelingCharacterRevision{};
    int hirelingStrength{},hirelingDexterity{};
    int hirelingVitality{};
    uint64_t hirelingNextExperience{};
    std::map<std::string,int64_t,std::less<>> hirelingBaseAttributes;
    unsigned hirelingPassiveMask{};
    int hirelingMovementPercent{};
    UnitCombatStats petStats;
    std::optional<NativeUnitStats> nativeStats;
    std::shared_ptr<const StatList> birthProperties;
    bool birthPropertiesPrepared{};
    std::optional<WeaponDamage> petWeapon;
    std::optional<EquipmentStats> equipmentStats;
    std::shared_ptr<const PersistentCharacter> equipment;
    std::vector<ItemAuraTimer> itemAuraTimers;
    std::vector<ItemStatEvent> itemStatEvents;
    std::vector<ChargedSkill> nativeCharges;
    std::vector<int> nativeSkillOrder; // SKILLS_AssignSkill appends; AI never sorts the list by ID.
    bool nativeChargesTracked{}; // Updated at the item stat callback, not inferred from final equipment.
    std::vector<SkillAuraChange> preparedItemAuras; // Unconsumed preparation callbacks, not final aura totals.
    int64_t life{}, maximumLife{};
    bool modeAlive{}; // Zero-HP native units remain active until a death mode.
    std::optional<EntityId> delayedDeathSource;
    bool alive() const {return life>0 || modeAlive;}
    bool hydra() const {return implementation==MonsterKind::Hydra1 || implementation==MonsterKind::Hydra2 || implementation==MonsterKind::Hydra3;}
    unsigned admittedPlayerCount = 1;
    std::deque<Vec> route;
    NativePathStep pathStep;
    uint64_t busyUntil{}, deathTick{}, deathOccurrence{};
    uint64_t nativeActionRevision{},nativeActionInterruption{},nativeActionUntil{};
    std::optional<uint64_t> neutralAt;
    EntityId killer, movementTarget; // Native path target persists across walking/attack/BL stops.
    EntityId hirelingKiller;
    int hirelingLootMagicFind{},hirelingLootGoldFind{};
    std::optional<OpenWoundsApplication> wound;
    Vec movementGoal;
    uint64_t hitOccurrence{};
    uint64_t wanderTargetOrder{};
    unsigned selfResurrections{};
    uint64_t combatRandom{};
    MonsterEnchantmentDamageState enchantmentDamage;
    uint64_t interruption{};
    uint8_t reactionMode{};
    uint64_t reactionUntil{};
    std::array<uint8_t,16> components{};
    bool shield{};
    int stopDistance{}, velocityPercent{75};
    uint8_t pathSteps{};
    uint8_t pathType{13}, pathDistance{5};
    int effectVelocity{};
    bool running{};
    bool rewardComplete{}, moving{};
    uint64_t riseUntil{};
    uint8_t riseMode{9};
    CorpseState corpse;
    bool shatterActive{};
    uint64_t holyFreezeUntil{};
    uint64_t webUntil{};
    bool burrowed{};
    bool airborne{}, holdPose{};
    uint8_t aiPose{1}; uint64_t poseUntil{};
    int64_t aiRegenerationBonus{};
    std::set<int> aiStates;
    struct SkillMotion {Vec destination;uint64_t until{},interruption{};float speed{};bool leap{};std::deque<Vec> route;EntityId target{};bool moveState{};};
    std::optional<SkillMotion> skillMotion;
    int nestSpawned{};
    Vec home;
    std::vector<Vec> skillPositions;
    uint64_t damageOccurrence{};
    bool lightningReady{};
    uint64_t chilledUntil{}, frozenUntil{}, nextHitTick{};
    uint64_t stunnedUntil{};
    uint64_t knockedUntil{};
    Vec knockbackSource;
    std::optional<Vec> knockbackGoal;
    std::optional<PoisonStatus> poison;
    UnitAlignment alignment() const {
        if(nativeStats && rule.stats) {
            const auto stat=rule.stats->names.find("alignment");
            if(stat!=rule.stats->names.end()) return UnitAlignment(nativeStats->values.get({stat->second,0}));
        }
        return owner || conversion?UnitAlignment::Good:UnitAlignment(rule.alignment);
    }
    bool enemyTarget() const { return hostile && !sameAlignment(UnitAlignment::Good, alignment()) && !rule.environmentController; }
    bool damageable() const { return !burrowed && !airborne && (questNpc?rule.stateTarget.attackable:((hostile && !rule.environmentController) || (rule.ai.kind==MonsterAiKind::GoodNpcRanged && rule.stateTarget.attackable) || amazonPet || (assassinPet && assassinPet->definition->attackable) || (druidPet && druidPet->definition->attackable) || necroSummon || hireling || conversion)); }
    bool combatCompanion() const { return bool(amazonPet) || bool(assassinPet) || bool(druidPet) || necroSummon || hireling || conversion; }
    bool amazonAttacker() const { return amazonPet && !amazonPet->decoy; }
    bool standardAttackSource() const { return !owner || hireling || amazonAttacker() || assassinPet || druidPet || necroSummon || conversion || hydra(); }
};
int movementPercent(const Actor &, uint64_t tick);
struct ActorPublication {
    std::map<EntityId,Actor> actors;
    std::map<EntityId,uint64_t> expected;
    uint64_t cursor{};
    size_t allocatedIds{};
    std::shared_ptr<EntityIds::Publication> identities;
    std::map<EntityId,uint64_t> removals;
    std::set<EntityId> restoredIdentities;
    std::optional<std::set<size_t>> uniquesExpected,uniquesNext;
};
using ComponentPaletteState=std::map<RegionId,std::map<int,MonsterComponentPalette>>;
struct NativeBirthBatch {
    std::map<EntityId,Actor> actors;
    ComponentPaletteState palettes;
    std::set<size_t> limitedUniques;
};
struct SkillChildrenExecution {
    std::vector<EntityId> children;
    size_t nextAura{};
    bool published{},complete{};
};
struct InactiveMonster {
    EntityId id;MonsterIdentity identity;std::optional<MonsterKind> implementation;
    RegionId area;Vec position;std::shared_ptr<const MonsterRule> admissionRule;
    uint64_t frame{},revision{};
    int64_t life{},maximumLife{};
    CorpseState corpse;
    unsigned selfResurrections{};
    int32_t experience{};
    int alignment{};
    bool hostile{},dead{},resurrected{},noTreasure{},noExperience{},uncounted{};
    bool ownsMinions{},transferMinions{},wander{},special{};
    std::optional<EntityId> minionOwner;
    std::vector<EntityId> minions;
    std::vector<int> modifiers;
    uint16_t nameSeed{};
    std::vector<Vec> skillPositions;
    int componentParameter{};
    int nestSpawned{};
};
struct State {
    std::map<EntityId, Actor> actors;
    std::map<std::pair<RegionId,size_t>,std::vector<InactiveMonster>> inactive;
    uint64_t nextSpecialTargetOrder{1};
    uint64_t nextWanderTargetOrder{1};
    uint64_t nextPetRegistrationOrder{1};
    // Scoped to the actual instance/area/class, including dynamically born units.
    ComponentPaletteState componentPalettes;
};
struct Ports { AreaStore &areas; const PlayerStore &players; EntityIds &ids; uint64_t &random; EventOutbox &events; const GameSettings &settings; effects::System &effects; const social::System &social; FrameEventQueue &frameEvents; std::shared_ptr<const SkillRules> skillPrograms; loot::System &loot; };
class System {
    friend class NativeBirthPublication;
    State state_;
    const Ports ports_;
    std::optional<ModifierPreparation> modifierPending_;
    std::optional<PreparedModifiers> modifierPrepared_;
    uint64_t nextModifierToken_{1};
    bool modifierCommitted_{};
    std::map<uint64_t,std::pair<EquipmentPreparation,std::optional<PreparedEquipment>>> birthEquipment_;
    uint64_t nextEquipmentToken_{1};
    std::optional<InactiveMonster> restoring_;
    DomainResult<> finishRestoration(const InactiveMonster &,uint64_t tick);
    std::optional<Vec> nestBirthPoint(EntityId,Vec,uint64_t &) const;
    bool recacheHirelingRoom(Actor &);
    ComponentPaletteState prepareComponents(Actor &,const ComponentPaletteState *staged=nullptr) const;
    void publishComponents(ComponentPaletteState &) noexcept;
    DomainResult<Actor> prepareAdmissionActor(const Admission &,EntityId,uint64_t &random,ComponentPaletteState &,uint64_t itemCursor=0,const std::set<size_t> *stagedUniques=nullptr);
    DomainResult<> prepareAdmissionEquipment(Actor &,uint64_t &random,uint64_t itemCursor,const std::set<size_t> *stagedUniques);
    void registerPet(Actor &) noexcept;
    void publishInitialAi(Actor &) noexcept;
    void prepareInitialTimers(Actor &,uint64_t) const;
    std::vector<FrameEventQueue::PreparedInsertion> prepareDeathSweep(Actor &,uint64_t tick) const;
    DomainResult<Actor> prepareDeathActor(EntityId,uint64_t tick) const;
    DomainResult<Actor> prepareDeathActor(const Actor &,uint64_t tick) const;
  public:
    void hirelingProjection(EntityId id,uint64_t next,unsigned passives,int movement,std::map<std::string,int64_t,std::less<>> base) {
        if(auto it=state_.actors.find(id);it!=state_.actors.end()) {it->second.hirelingNextExperience=next;it->second.hirelingPassiveMask=passives;it->second.hirelingMovementPercent=movement;it->second.hirelingBaseAttributes=std::move(base);}
    }
    explicit System(Ports ports) : ports_(ports) {}
    const SkillRules *skillPrograms() const {return ports_.skillPrograms.get();}
    std::optional<EquipmentPreparation> pendingEquipment();
    DomainResult<> prepareEquipment(PreparedEquipment);
    size_t equipmentPending() const {return birthEquipment_.size();}
    std::string equipmentFailure() const {
        for(const auto &[token,entry]:birthEquipment_) if(entry.second && !entry.second->deferred.empty()) return entry.second->deferred;
        return {};
    }
    DomainResult<std::shared_ptr<NativeBirthPublication>> prepareLevelSpawn(RegionId,Vec roomPosition,Vec requested,uint64_t tick,
        const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision);
    DomainResult<std::shared_ptr<NativeBirthPublication>> prepareRoomSpawn(const Admission &,RoomRandomLoan,int attempts,
        std::optional<size_t> logicalRegion,bool probe,
        const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision);
    DomainResult<std::optional<Vec>> searchRoomSpawn(const Admission &,RoomRandomLoan &,int attempts,
        const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision);
    DomainResult<std::shared_ptr<NativeBirthPublication>> prepareTrapController(const Admission &,
        const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision);
    DomainResult<EntityId> stageRoomSpawn(const Admission &,RoomRandomLoan &,int attempts,EntityIds &,uint64_t &random,
        NativeBirthBatch &,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision);
    void publishBirthComponents(NativeBirthBatch &) noexcept;
    DomainResult<> compressRoom(RegionId,size_t,uint64_t tick);
    StepStatus restoreRoom(RegionId,size_t,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &,bool sanctumCleared);
    const State &read() const { return state_; }
    ActorPublication prepareActorPublication(std::map<EntityId,Actor>,size_t allocatedIds,std::span<const EntityId> restored={},
        std::shared_ptr<EntityIds::Publication> identities={},std::span<const EntityId> retired={}) const;
    bool validateActors(const ActorPublication &) const;
    void publishActors(ActorPublication &) noexcept;
    const Actor *find(EntityId id) const { auto it = state_.actors.find(id); return it == state_.actors.end() ? nullptr : &it->second; }
    void admitNpc(EntityId,RegionId,Vec,const NpcRule &);
    DomainResult<> moveNpc(EntityId,RegionId,Vec,Vec,bool escaped);
    DomainResult<> regenerate(EntityId,uint64_t tick);
    bool frozen(EntityId,uint64_t tick) const;
    EntityId minionOwner(EntityId) const;
    std::vector<EntityId> specialTargets() const;
    std::vector<EntityId> pets(PlayerId) const;
    bool canRegisterSpecialTargets(size_t count) const noexcept {return count<=UINT64_MAX-state_.nextSpecialTargetOrder;}
    DomainResult<> beginAttack(EntityId, uint64_t until, bool returnToNeutral = false, EntityId target = {},std::optional<NativeUnitStats> modeStats = {});
    DomainResult<> extendAttack(EntityId,uint64_t until);
    DomainResult<> finishDamageAction(EntityId,EntityId source,int64_t amount,uint64_t tick,uint8_t hitClass,bool softHit,std::optional<bool> getHit);
    uint64_t resumeAi(EntityId, uint64_t tick, uint64_t scheduled);
    void finishAction(EntityId,uint64_t tick);
    void scheduleAi(EntityId,uint64_t due);
    void roomClientsChanged(RoomView,bool present,uint64_t tick);
    void resumeMode(EntityId,uint64_t tick);
    DomainResult<> modeEvent(const FrameEvent &,uint64_t tick);
    StepStatus advanceMode(EntityId,TickContext);
    DomainResult<> attackProfile(EntityId,std::shared_ptr<const StatList>,uint64_t expected,uint64_t next);
    DomainResult<> resetAiSelector(EntityId,uint64_t timer,uint64_t tick);
    DomainResult<> schedulePresetReanimation(EntityId,uint64_t tick);
    DomainResult<> modifierEvent(const FrameEvent &,uint64_t tick,const std::function<uint16_t(RoomView,int32_t,int32_t)> &collision);
    bool resurrectionTarget(EntityId source,EntityId corpse,uint64_t tick) const;
    bool nativeUsableCorpse(EntityId,uint64_t tick) const;
    bool usableCorpse(EntityId,RegionId,uint64_t,bool revive=false) const;
    bool deadMode(EntityId,uint64_t tick) const;
    bool selectableCorpse(EntityId,uint64_t tick) const;
    bool consumableCorpse(EntityId,RegionId,uint64_t,bool revive=false) const;
    void consumeCorpse(EntityId) noexcept;
    void searchCorpse(EntityId id) noexcept {if(auto it=state_.actors.find(id);it!=state_.actors.end()) {it->second.corpse.searched=true;++it->second.revision;}}
    void refreshNecro(EntityId,MonsterRule,UnitCombatStats,std::shared_ptr<const NecroPetSpec>,std::optional<WeaponDamage>,std::optional<NativeUnitStats> = {},uint64_t tick=0);
    void curseControl(EntityId id,Actor::CurseControl control);
    void attract(EntityId id,EntityId target,uint64_t until) noexcept {if(auto it=state_.actors.find(id);it!=state_.actors.end()) {it->second.attractionTarget=target;it->second.attractionUntil=until;}}
    DomainResult<> resurrect(EntityId source,EntityId corpse,uint64_t tick);
    bool mummySupportTarget(EntityId source,EntityId target,uint64_t tick,bool dead,bool healthy=false) const;
    DomainResult<> spawnSkillChildren(EntityId source,Vec target,int count,bool hatch,uint64_t tick,std::shared_ptr<SkillChildrenExecution> &);
    DomainResult<> burrow(EntityId source,bool below,uint64_t tick);
    DomainResult<> startSkillMotion(EntityId source,Vec target,uint64_t until,int skill,bool leap,uint64_t tick,EntityId victim={});
    DomainResult<> startSwarmMove(EntityId source,Vec target,uint64_t tick);
    bool advanceSkillMotion(Actor &,uint64_t tick);
    bool canLand(EntityId) const;
    DomainResult<> consumeRandom(EntityId,uint64_t expected,uint64_t next);
    DomainResult<> finishSuperUniqueBirth(EntityId,bool corpseUnavailable);
    DomainResult<> aiPose(EntityId,uint8_t mode,bool hold,bool airborne,uint64_t tick);
    DomainResult<> aiColor(EntityId,int color,int overlay,uint64_t tick);
    void aiRegeneration(EntityId,int eighths);
    DomainResult<EntityId> spawnNestChild(EntityId,uint64_t tick);
    DomainResult<EntityId> spawnNestChild(EntityId,uint64_t tick,Vec origin);
    bool canSpawnNestChild(EntityId) const;
    DomainResult<> teleport(EntityId,Vec,uint64_t tick,int heal = 0,bool nativeFreePosition = false);
    DomainResult<> activateWeb(EntityId,int skill,int rank,uint64_t tick);
    DomainResult<> heal(EntityId,int64_t amount);
    DomainResult<> healSkill(EntityId source,EntityId target,int64_t,uint64_t,std::pair<uint64_t,uint64_t>);
    void maximumLife(EntityId,int64_t);
    bool canCommitLife(const LifeChange &) const;
    bool commitLife(LifeChange &&) noexcept;
    DomainResult<> convert(EntityId,const ActorContext &,const NativeConversionSpec &,LifeChange,std::optional<ConversionResourceSave>,std::vector<DomainFact>);
    DomainResult<> finishConversion(EntityId,uint64_t,LifeChange,std::vector<DomainFact>);
    DomainResult<> holyFreeze(EntityId,uint64_t until,uint64_t tick);
    DomainResult<> slow(EntityId,int state,int percent,uint64_t frames,uint64_t tick,EntityId source={});
    // coldFrames is the raw hit duration; qualification/resistance/difficulty
    // are resolved here for every producer, before committing lethal damage.
    DomainResult<> damage(EntityId, EntityId source, int64_t amount, uint64_t tick, uint64_t coldFrames = 0, bool freeze = false, uint8_t hitClass = 0, std::optional<PoisonApplication> poison = {},bool poisonOnly=false,uint64_t stunFrames=0,std::optional<OpenWoundsApplication> wound={},int64_t absorbed=0,bool clearStun=false,int64_t crushing=0,int coldPierce=0,bool softHit=false,std::optional<uint64_t> freezeLength={},std::optional<bool> getHit={},DamageStatus status={});
    void shortenPoison(EntityId,uint64_t tick,int remainingPercent);
    void drainResources(EntityId,int32_t mana,int32_t stamina,uint64_t tick);
    void disableTreasure(EntityId);
    DomainResult<> block(EntityId,uint64_t tick);
    DomainResult<> lightningEmission(EntityId,bool emitted,uint64_t tick);
    void rewardComplete(EntityId);
    void recordHirelingKill(EntityId victim,EntityId source) {
        const auto *killer=find(source);
        if(!killer || !killer->hireling) return;
        if(auto found=state_.actors.find(victim);found!=state_.actors.end() && found->second.life<=0) {
            found->second.hirelingKiller=source;
            found->second.hirelingLootMagicFind=killer->petStats.attributes.combat.magicFind;
            found->second.hirelingLootGoldFind=killer->petStats.attributes.combat.goldFind;
        }
    }
    void commitCombatRandom(EntityId id, uint64_t value) { if (auto it=state_.actors.find(id); it!=state_.actors.end()) it->second.combatRandom=value; }
    void commitEnchantmentDamage(EntityId id,MonsterEnchantmentDamageState value) { if(auto it=state_.actors.find(id);it!=state_.actors.end()) it->second.enchantmentDamage=std::move(value); }
    DomainResult<> introduction(EntityId,uint64_t tick);
    void velocityModifier(EntityId id,int percent) { if(auto it=state_.actors.find(id);it!=state_.actors.end()) {if(it->second.effectVelocity!=percent) {it->second.effectVelocity=percent;++it->second.revision;}} }
    DomainResult<EntityId> admit(const Admission &);
    DomainResult<> upgradeShrine(EntityId,uint64_t tick);
    DomainResult<> prepareBirthModifiers(EntityId,MonsterRank,std::vector<int>,std::span<const EntityId>,uint64_t tick,
        std::optional<std::pair<uint64_t,uint64_t>> random={},bool promoteMinions=false,std::optional<uint16_t> nameSeed={});
    std::optional<ModifierPreparation> pendingModifiers() const {return modifierPrepared_?std::nullopt:modifierPending_;}
    bool modifiersPending() const {return modifierPending_.has_value();}
    std::string modifierFailure() const {return modifierPrepared_?modifierPrepared_->deferred:std::string{};}
    DomainResult<> prepareModifiers(PreparedModifiers);
    StepStatus finishModifiers(TickContext);
    DomainResult<> finishTrap(EntityId,uint64_t tick);
    DomainResult<> scriptDeath(EntityId,uint64_t tick);
    std::vector<Actor> prepareMinionDeaths(Actor,uint64_t tick,const std::map<EntityId,Actor> *staged=nullptr) const;
    DomainResult<> reduceShrineLife(EntityId,int64_t amount);
    DomainResult<> requestMove(const MoveRequest &);
    void stop(EntityId);
    DomainResult<> toggleAiState(EntityId,int state,bool active,uint64_t tick);
    void hitDelay(EntityId id, uint64_t until);
    void knockback(EntityId, Vec source, uint64_t tick);
    DomainResult<> missileReaction(EntityId target,EntityId source,bool getHit,bool knockback,uint8_t hitClass,uint64_t tick);
    bool canRemove(EntityId) const;
    DomainResult<> remove(EntityId);
    void withdrawQuestGroup(RegionId,std::string_view);
    DomainResult<std::map<EntityId,Actor>> prepareHydra(const ActorContext &, const HydraRules &,const SummonCastSpec &,const std::array<std::optional<Vec>,3> &,uint64_t &random) const;
    DomainResult<std::map<EntityId,Actor>> prepareHireling(const ActorContext &,const MonsterRule &,std::string_view,Vec,int64_t life,uint64_t random,NativeUnitStats,int birthLevel,NativeBirthBatch &,std::span<const EntityId> retired={}) const;
    DomainResult<std::map<EntityId,Actor>> prepareNativePet(const ActorContext &,const MonsterRule &,Vec,const SummonCastSpec &,std::shared_ptr<PersistentCharacter>,size_t,std::optional<WeaponDamage> = {},std::span<const std::shared_ptr<const StatList>> = {},uint64_t random=0,std::optional<EquipmentStats> = {}) const;
    DomainResult<std::map<EntityId,Actor>> prepareNecro(const ActorContext &,const MonsterRule &,Vec,const SummonCastSpec &,uint64_t,size_t offset=0,std::span<const std::shared_ptr<const StatList>> = {},std::optional<MonsterComponents> components={},std::optional<bool> shield={}) const;
    DomainResult<> retire(EntityId, uint64_t tick);
    DomainResult<> stageRetirements(effects::AuraUnitPublication &,std::span<const EntityId>,uint64_t tick) const;
    DomainResult<> updateHireling(EntityId,MonsterRule,WeaponDamage,CombatModifiers,int strength,int dexterity,int vitality,
        std::shared_ptr<const PersistentCharacter>,uint64_t inventoryRevision,uint64_t characterRevision,NativeUnitStats,Fixed8 maximum);
    DomainResult<> warpPet(EntityId,const ActorContext &,Vec);
    std::optional<std::pair<Vec,int>> targetPosition(EntityId,RegionId) const;
};
}

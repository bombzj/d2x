#pragma once
#include <span>
#include "server/runtime/contracts.hpp"
#include "skill_state.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/effects/definition.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "gameplay/units/restoration.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/conversion_spec.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/monsters/damage.hpp"
#include "gameplay/combat/poison.hpp"
#include "gameplay/combat/open_wounds.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "gameplay/combat/reception.hpp"
#include "gameplay/consumables/potions.hpp"
#include "server/systems/monsters/life_change.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "server/systems/actions/state.hpp"
#include <array>
#include <list>
#include <initializer_list>
#include <algorithm>
#include <functional>
#include <map>
#include <utility>
namespace d2x::server {struct PlayerState;}
namespace d2x {struct WeaponSkillDamage;struct AssassinCharge;}
namespace d2x {struct AuraSkillSpec;}
namespace d2x::server::monsters {struct Actor;struct ActorPublication;}
namespace d2x::server::combat {struct SpellImpact;}
namespace d2x::server::attributes {class MonsterResourceTrace;}
namespace d2x::server::effects {
inline constexpr uint32_t monsterModifierTimerHandler=0x4d4f4454;
inline constexpr uint32_t modeEndTimerHandler=0x4d454e44;
struct BladeShieldPulse;
struct MindBlastPulse;
struct CorpseExplosionPulse;
struct NativeAreaSpec;struct NativeAreaPulse;
struct MonsterCursePulse {
    AuraDefinition aura;CurseSpec program;int32_t length{};uint64_t tick{};
    std::vector<EntityId> targets;size_t next{};bool prepared{},complete{};
};
std::vector<StatValue> nativeEffectStats(const SkillRules &, const CharacterModifiers &);
std::vector<StatValue> nativeEffectStats(const SkillRules &,const CombatEffectSpec &);
std::vector<StatValue> nativeEffectStats(const StatRuleCatalog &,const CombatEffectSpec &);
struct Recovery { CombatEffectSet states; std::optional<PoisonStatus> poison; uint64_t nextMissileHit{},poisonRevision{}; std::optional<OpenWoundsApplication> wound; EntityId killer; std::string killerName,killerClass; };
struct PlayerPoisonCandidate {std::optional<PoisonStatus> poison;uint64_t revision{};bool changed{};};
struct RightAuraTimerChange {bool replace{};std::optional<FrameEvent> event;};
struct PlayerEffectsPublication {EntityId actor;CombatEffectSet states;std::map<EntityId,Recovery> insertion;std::optional<std::optional<PoisonStatus>> poison;std::optional<uint64_t> expectedPoisonRevision;FrameEventQueue::PreparedEdit timers,skillTimers;};
struct PlayerHitAdjustment {
    int coldPierce{},poisonPierce{};
    int64_t crushing{};
    std::optional<OpenWoundsApplication> wound;
    EntityId credit;
};
struct ReceivedDamage {float dealt{};int64_t physical{};};
// The native damage calculation runs before unit-event callbacks and HP
// subtraction. Keep its owned result across synchronous callbacks/backpressure.
struct PlayerReceptionPlan {
    ActorContext actor;
    uint64_t inventoryRevision{},characterRevision{};
    Recovery recovery;
    CharacterRecord character;
    TransientAttributes transient;
    ResourceMaximums maximum;
    DamageReception damage;
    std::optional<std::pair<uint64_t,uint64_t>> random;
    CharacterAttributes defenses;
    int poisonPierce{};
    attributes::Totals totals;
    std::vector<DomainFact> facts;
};
struct NativeContactCallback {EntityId unit,other;TriggeredSkillUnitEvent event;};
struct NativeContactBatch {
    EntityId source,target;
    RegionId area;
    int64_t physical{},total{};
    std::vector<NativeContactCallback> callbacks;
    size_t next{};
    uint64_t pendingOccurrence{};
    bool queued{};
    std::shared_ptr<combat::SpellImpact> pendingDamage;
    std::optional<std::pair<uint64_t,uint64_t>> random;
    bool randomCommitted{};
    bool callbackStarted{};
    unsigned phase{};
    unsigned eventStage{};
    bool missile{},returnFire{},suppress{},suppressAttacker{};
    int32_t thorns{};
    int64_t remaining{};
    int share{},overlay{-1};
    EntityId owner;
};
struct State { std::map<EntityId, Recovery> players; std::string itemDeferred,itemAuraDeferred; };
struct UnitEffect {
    RegionId area;
    CombatEffectSet states;
    // Derived wire/change-detection cache; numeric queries read states.
    std::map<int,std::vector<StatValue>> nativeStats;
    bool converted{};
    int64_t baseLife{}, adjustedLife{};
};
struct UnitDamageStates {
    EntityId source;
    uint64_t chilledUntil{},frozenUntil{},stunnedUntil{};
    int64_t burn{};uint64_t burnFrames{};
    std::optional<uint64_t> freezeThinkAt;
    bool curePoison{},cureCold{};
    std::optional<PotionDefinition> potion;
    std::optional<int32_t> potionRestoration;
    std::optional<CombatEffectSpec> status;
    std::vector<StatValue> statusWrites;
    bool clearStun{},poisonApplied{},woundApplied{},resetRegeneration{},contact{},deathDelay{};
    std::vector<int> clearStateFlags;
};
struct AuraTargetProgress {bool state{},accepted{},pulse{},post{};};
enum class NativeAuraTargetMode { OwnerRange,TargetRange };
struct NativeAuraTargetQuery {
    EntityId owner;RegionId area;int32_t radius{};uint32_t filter{};bool checkNoAura{};
    std::optional<Vec> center; // Native global coordinates, distinct from ray origin.
    NativeAuraTargetMode mode{NativeAuraTargetMode::OwnerRange};
    std::optional<uint64_t> tick; // Required by the native JUSTHIT finder flag.
    std::optional<Vec> sourcePosition; // Global missile position; owner identity still resolves allegiance.
};
struct AuraUnitPublication {
    struct RecipientGuard {uint64_t areaGeneration{};};
    std::map<EntityId,RecipientGuard> recipients;
    struct SkillEventRegistration {EntityId unit;int state;EffectSource source;std::array<SkillUnitEventBinding,3> bindings;};
    std::vector<SkillEventRegistration> skillEvents;
    std::set<EntityId> eventsOnly;
    using SourceMutation=std::function<void(monsters::Actor &,attributes::MonsterResourceTrace &,const SkillAuraChange &)>;
    using SourcePreparation=std::function<void(monsters::Actor &,const CombatEffectSet &,const SourceMutation &)>;
    std::shared_ptr<monsters::ActorPublication> actors;
    std::map<EntityId,SourcePreparation> sources;
    // Borrowed only during the synchronous preparation callback. Publication
    // rejects a plan whose source preparation has not unwound.
    std::map<EntityId,attributes::MonsterResourceTrace *> activeSources;
    std::map<EntityId,UnitEffect> units;
    std::map<EntityId,monsters::LifeChange> life;
    std::vector<EntityId> lifeOrder;
    std::map<EntityId,CombatEffectSet> expectedEffects;
    std::set<EntityId> expectedAbsent;
    std::map<EntityId,int> velocity;
    std::vector<DomainFact> facts;
    std::map<EntityId,FrameEventQueue::PreparedEdit> timers;
    std::map<EntityId,FrameEventQueue::PreparedEdit> resetTimers;
    std::map<EntityId,FrameEventQueue::PreparedEdit> skillTimers;
    std::map<EntityId,FrameEventQueue::PreparedEdit> nativeSkillTimers;
    std::map<EntityId,FrameEventQueue::PreparedEdit> regenerationTimers;
    std::set<EntityId> resetRegeneration;
    uint64_t tick{};
};
struct UnitReceptionPlan {
    EntityId target;
    uint64_t tick{};
    DamageReception damage;
    std::shared_ptr<AuraUnitPublication> publication;
};
// Prepared before inventory consumption, installed without allocation only after
// the character transaction succeeds. No callback can observe half a drink.
struct PotionPlan { State next; CharacterRecord character; TransientAttributes transient; std::optional<std::pair<uint64_t,uint64_t>> unitRandom{}; std::vector<DomainFact> publicFacts; std::optional<ResourceMaximums> resourceBase; std::shared_ptr<AuraUnitPublication> units; bool staminaPotionSurplus{}; };
struct WellRestoration { bool usesCharge{}; };
struct Ports {
    const PlayerStore &players; const AreaStore &areas; skills::System &skills;
    transactions::System &transactions; missiles::System &missiles; combat::System &combat; monsters::System &monsters; EventOutbox &events; uint64_t &random; const social::System &social;
    reactions::ItemTriggers &itemTriggers;
    actions::State &actions;
    FrameEventQueue &frameEvents;
};
class System {
    CharacterAttributes unitDefenses(EntityId,uint64_t) const;
    State state_;
    std::optional<uint64_t> frame_;
    unsigned framePhase_{};
    uint64_t nextAuraEventOrder_{1};
    struct PlayerFrame {bool complete{};};
    std::map<EntityId,PlayerFrame> framePlayers_;
    void publishPlayers(std::map<EntityId,Recovery> &) noexcept;
    DomainResult<> nativeTerror(const ActorContext &,const SkillCastSpec &,EntityId);
    DomainResult<std::shared_ptr<AuraUnitPublication>> prepareFear(const NativeFear &,uint64_t tick) const;
    DomainResult<> nativeAttract(const ActorContext &,const SkillCastSpec &,EntityId);
    const Ports ports_;
    reactions::ItemTriggers &itemTriggers_;
    StepStatus advanceItemTriggers(uint64_t tick) {
        const auto result = itemTriggers_.advance(tick);
        state_.itemDeferred = itemTriggers_.deferred();
        return result;
    }
    std::map<EntityId,UnitEffect> units_;
    FrameEventQueue::PreparedEdit prepareStormTimers(const ActorContext &,const PlayerState &,const CombatEffectSet &,
        const attributes::Totals &,const CharacterRecord &,const RightAuraTimerChange &) const;
    struct StormPulse {
        SkillCastSpec skill;
        EntityId target,projectile;
        RegionId area{};Vec position;
        std::vector<DomainFact> notices;
        std::shared_ptr<NativeAreaPulse> damage;
        FrameEventQueue::PreparedInsertion next;
        bool prepared{},launched{},damaged{},notified{},rescheduled{};
    };
    std::map<FrameEventHandle,StormPulse> stormPulses_;
    std::map<std::pair<EntityId,int>,EntityId> stormLast_;
    DomainResult<> stormEvent(const FrameEvent &,uint64_t);
    DomainResult<> bladeShieldEvent(const FrameEvent &,uint64_t);
    DomainResult<> advanceBladeShield(BladeShieldPulse &,uint64_t);
    std::map<FrameEventHandle,std::pair<FrameEventOwner,std::shared_ptr<BladeShieldPulse>>> bladeShieldPulses_;
    uint64_t bladeShieldOccurrence_{};
    uint64_t mindBlastOccurrence_{};
    uint64_t corpseExplosionOccurrence_{};
    uint64_t nativeAreaOccurrence_{};
    DomainResult<> advanceNativeCallback(NativeContactBatch &,uint64_t);
    struct PaladinCycle {
        int skill{-1},rank{}; RegionId area{}; uint64_t next{},occurrence{},duration{},eventOrder{};
        AuraDefinition aura; std::vector<EntityId> targets; size_t target{};
        bool pending{},pulseCommitted{},callbackAffected{},manaCommitted{}; uint64_t random{};
        AuraTargetProgress progress;std::optional<int32_t> damage;bool recipientsPrepared{};
        std::optional<ItemAuraTimer> itemTimer,itemReplacement;
        bool itemDecisionPrepared{},itemCancel{},itemReplacementPrepared{};
    };
    std::map<EntityId,PaladinCycle> paladinCycles_;
    std::map<EntityId,PaladinCycle> unitAuraCycles_;
    std::map<std::pair<EntityId,EntityId>,PaladinCycle> itemAuraCycles_; // (unit, timer custom GUID).
    uint64_t necroOccurrence_{};
    DomainResult<> unitAuraEvent(const FrameEvent &,uint64_t);
    StepStatus advanceUnitAuraCycle(const ActorContext &,EntityId,PaladinCycle &,uint64_t);
    AuraDefinition evaluateUnitAura(EntityId,int skill,int rank,uint64_t tick) const;
    DomainResult<> queueAuraPulse(EntityId,const AuraDefinition &,const std::vector<EntityId> &,uint64_t occurrence,int64_t damage);
    DomainResult<int32_t> rollAuraDamage(EntityId,const AuraDefinition &,uint64_t tick);
    DomainResult<> advanceAuraTarget(const ActorContext &,EntityId source,const AuraDefinition &,EntityId target,uint64_t duration,
        uint64_t occurrence,int32_t damage,AuraTargetProgress &,bool owner,bool *affected=nullptr);
    DomainResult<> holyFreezeShatter(EntityId,uint64_t duration,uint64_t tick);
    uint64_t paladinOccurrence_{};
    DomainResult<> rightAuraEvent(const FrameEvent &,uint64_t);
    DomainResult<> itemAuraEvent(const FrameEvent &,uint64_t);
    StepStatus advanceAuraCycle(const ActorContext &,const PlayerState &,const AuraSkillSpec &,PaladinCycle &,uint64_t);
    DomainResult<> paladinAura(const ActorContext &,const AuraDefinition &,EntityId,uint64_t,bool owner=false,bool healing=false,EntityId emitter={},bool *committedAffected=nullptr);
    DomainResult<> consumeAuraMana(const ActorContext &,const AuraDefinition &,bool affected);
    DomainResult<> removeAura(const ActorContext &,int skill);
    std::map<EntityId,uint8_t> movementSkillPhases_;

    struct PendingMonsterCurse {AuraDefinition aura;SkillFormulaSnapshot source;std::vector<EntityId> targets;size_t next{};uint64_t tick{};};
    std::deque<std::pair<EntityId,PendingMonsterCurse>> monsterCurses_;
    StepStatus advanceMonsterCurses(uint64_t);
    struct ModifierExecution;
    struct UniqueCycle {uint64_t nextLightning{};bool emissionPending{};std::optional<uint64_t> queenDeath;std::shared_ptr<ModifierExecution> deathExecution;};
    std::map<EntityId,UniqueCycle> uniqueCycles_;
    struct ModifierExecution {
        EntityId owner,missile;std::vector<int> callbacks;size_t next{};
        bool captured{},damagePrepared{};
        std::shared_ptr<NativeAreaPulse> area;
    };
    std::map<FrameEventHandle,ModifierExecution> modifierExecutions_;
    DomainResult<> executeMonsterModifier(EntityId,int,uint64_t,ModifierExecution &);
    DomainResult<> boneDeathEffect(EntityId,uint64_t,ModifierExecution &);
    DomainResult<> monsterAura(EntityId,const AuraDefinition &,EntityId,uint64_t,uint64_t duration,bool owner=false);

    StepStatus advanceUnits(uint64_t);
  public:
    void prepareMonsterModifierEvents(monsters::Actor &,const monsters::Actor *) const;
    DomainResult<> monsterModifierEvent(const FrameEvent &,uint64_t);
    DomainResult<> monsterDamageEffect(EntityId,uint64_t);
    DomainResult<> monsterDeathEffect(EntityId,uint64_t);
    DomainResult<> movementSkills(EntityId,uint64_t,uint8_t direction,Vec nativePosition);
    explicit System(Ports ports) : ports_(ports), itemTriggers_(ports.itemTriggers) {}
    void registerPlayer(EntityId,uint64_t tick);
    DomainResult<> regeneratePlayer(EntityId,uint64_t tick);
    void forgetUnit(EntityId) noexcept;
    AuraUnitPublication prepareDamageStates(monsters::Actor,UnitDamageStates,uint64_t tick) const;
    void stageDamageStates(AuraUnitPublication &,monsters::Actor,UnitDamageStates,uint64_t tick) const;
    const State &read() const { return state_; }
    PlayerEffectsPublication preparePlayerEffects(const ActorContext &,const CombatEffectSet &,const attributes::Totals &,const CharacterRecord &,const RightAuraTimerChange &right={}) const;
    PlayerPoisonCandidate preparePlayerPoison(EntityId) const;
    uint64_t reserveAuraEventOrder();
    bool validatePlayerEffects(const PlayerEffectsPublication &) const;
    void publishPlayerEffects(PlayerEffectsPublication &) noexcept;
    void prepareUnitTimers(AuraUnitPublication &) const;
    std::shared_ptr<FrameEventPublication> prepareItemAuraEvents(FrameEventOwner,std::span<const ItemAuraTimer>) const;
    bool validatePeriodicEvents(const FrameEventPublication &) const;
    void publishPeriodicEvents(FrameEventPublication &) noexcept;
    void cancelPeriodicSkills(FrameEventOwner) noexcept;
    DomainResult<> frameEvent(const FrameEvent &,uint64_t tick);
    std::vector<EntityId> auraCandidateRecipients(EntityId,const AuraDefinition &,RegionId,uint64_t tick,const monsters::Actor *source=nullptr) const;
    std::vector<EntityId> nativeAuraCandidateRecipients(NativeAuraTargetQuery,const monsters::Actor *source=nullptr) const;
    void prepareImmediateAuraUnit(AuraUnitPublication &,const ActorContext &,const AuraDefinition &,const SkillAuraChange &,EntityId,
        bool owner=false,const monsters::Actor *candidate=nullptr);
    using AuraRecipientPreparation=std::function<void(const ActorContext &,const AuraDefinition &,const SkillAuraChange &,EntityId)>;
    RightAuraTimerChange prepareRightAuraSelection(const ActorContext &,const PlayerState &,const CharacterRecord &,CombatEffectSet &,
        const attributes::AuraSourceObserver &,const attributes::AuraSourceReader &,PlayerPoisonCandidate &,std::vector<DomainFact> &,
        const AuraRecipientPreparation &,bool force);
    DomainResult<> assignUnitAura(EntityId,const AuraDefinition &,uint64_t tick,std::optional<int> assignedRank={});
    void prepareUnitAuraSelection(AuraUnitPublication &,EntityId,int skill,uint64_t tick,
        const AuraRecipientPreparation &,std::optional<int> assignedRank={});
    DomainResult<> assignUnitSkills(EntityId,std::span<const std::pair<int,int>>,uint64_t tick);
    void prepareUnitItemAura(AuraUnitPublication &,monsters::Actor &,const SkillAuraChange &,uint64_t,
        const AuraRecipientPreparation &);
    void prepareUnitItemAuras(AuraUnitPublication &,monsters::Actor &,uint64_t tick,const AuraRecipientPreparation &);
    void stageAuraActors(AuraUnitPublication &,std::map<EntityId,monsters::Actor>,size_t allocatedIds,std::span<const EntityId> retired={});
    void stageSummonEvents(AuraUnitPublication &,EntityId,int skill,int rank,const SkillFormulaCatalog &,uint64_t tick) const;
    void prepareStagedSkillEvents(AuraUnitPublication &,uint64_t tick) const;
    void stageAuraSource(AuraUnitPublication &,EntityId,AuraUnitPublication::SourcePreparation);
    void prepareStagedItemAuras(AuraUnitPublication &,uint64_t tick,const AuraRecipientPreparation &);
    void prepareUnitEffects(AuraUnitPublication &,EntityId,const UnitEffect &,std::optional<monsters::LifeChange>,uint64_t tick) const;
    bool validateAuraUnits(const AuraUnitPublication &) const;
    void publishAuraUnits(AuraUnitPublication &) noexcept;
    std::pair<uint8_t,uint8_t> potionRecoveryPercent(PlayerId,uint64_t) const;
    size_t pendingItemTriggers() const {return itemTriggers_.pending();}
    DomainResult<PotionPlan> potion(const ActorContext &, const PotionDefinition &) const;
    DomainResult<PotionPlan> skillState(const ActorContext &, CombatEffectSpec,std::span<const StatValue> writes={}) const;
    DomainResult<> frenzy(const ActorContext &,const SkillCastSpec &);
    DomainResult<> progressiveCharge(const ActorContext &,const SkillCastSpec &);
    bool hasProgressive(EntityId) const;
    std::vector<AssassinCharge> progressiveCharges(EntityId) const;
    DomainResult<> progressiveValue(const ActorContext &,const AssassinCharge &,int count);
    int32_t progressivePhysicalPercent(EntityId) const;
    void progressiveDamage(EntityId,WeaponSkillDamage &,uint64_t &random) const;
    DomainResult<PotionPlan> heal(const ActorContext &) const;
    DomainResult<WellRestoration> restoreWell(const ActorContext &,Fixed8 life,Fixed8 mana,Fixed8 stamina,const std::vector<NativeStateCure> &cureStates);
    void commit(PotionPlan &&plan) noexcept;
    DomainResult<> apply(const ActorContext &, CombatEffectSpec, bool restoreStamina = false,
        std::span<const StatValue> writes={},bool refreshPassives=false);
    DomainResult<> removeState(const ActorContext &,int state);
    DomainResult<> druidRage(const ActorContext &,const SkillCastSpec &);
    DomainResult<bool> removeStateGroup(const ActorContext &,int group);
    DomainResult<> removeUnitState(EntityId,int state,uint64_t tick);
    DomainResult<> prepareUnitCurseRemoval(AuraUnitPublication &,EntityId,uint64_t tick) const;
    DomainResult<> setNativeSkillState(EntityId,int state,bool active,uint64_t tick);
    DomainResult<> postModeRate(EntityId,int skill,int rank,int32_t rate,uint64_t revision,uint64_t tick,std::pair<uint64_t,uint64_t> random);
    DomainResult<> endMode(EntityId,uint64_t tick,std::optional<uint64_t> activeRevision={});
    void scheduleModeEnd(EntityId,uint64_t tick);
    DomainResult<> modeEndEvent(EntityId,uint64_t tick);
    DomainResult<> skill(const ActorContext &, const SkillCastSpec &, std::optional<PlayerId> recipient = {},std::span<const DomainFact> facts={});
    DomainResult<> bladeShield(const ActorContext &,const SkillCastSpec &,std::shared_ptr<BladeShieldPulse> &);
    bool bladeShieldStarted(const std::shared_ptr<BladeShieldPulse> &) const;
    DomainResult<> mindBlast(const ActorContext &,const SkillCastSpec &,Vec,std::shared_ptr<MindBlastPulse> &);
    DomainResult<> corpseExplosion(EntityId source,EntityId corpse,int skill,int rank,uint64_t tick,std::shared_ptr<CorpseExplosionPulse> &,const SkillCastSpec *payment=nullptr);
    DomainResult<> nativeArea(const NativeAreaSpec &,uint64_t tick,std::shared_ptr<NativeAreaPulse> &);
    DomainResult<> skillUnit(const ActorContext &, const SkillCastSpec &, EntityId);
    DomainResult<> curse(const ActorContext &,const SkillCastSpec &,Vec,EntityId);
    DomainResult<> unitSkill(EntityId,const SkillCastSpec &,uint64_t,std::optional<std::pair<uint64_t,uint64_t>> random={});
    DomainResult<> hirelingAura(EntityId,const AuraDefinition &,uint64_t);
    bool hirelingAuraActive(EntityId,int skill,int rank) const;
    DomainResult<> convert(const ActorContext &,EntityId,const WeaponSkillSpec &);
    DomainResult<> convert(const ActorContext &,EntityId,const NativeConversionSpec &);
    DomainResult<> endConversion(EntityId,uint64_t tick);
    DomainResult<> amazonMagic(const ActorContext &, const SkillCastSpec &,std::optional<EntityId> emitter = {});
    // Borrowed view of the affected unit's canonical skill/state lists.
    // Ownership/allegiance selects recipients; it does not select a new store.
    const CombatEffectSet *unitEffects(EntityId) const;
    CharacterModifiers unitModifiers(EntityId, uint64_t tick) const;
    CombatModifiers combatModifiers(EntityId,uint64_t tick) const;
    DomainResult<> skillAuraState(EntityId source,EntityId target,int skill,int rank,int32_t duration,uint64_t tick,
        NativeSkillStateProcedure=NativeSkillStateProcedure::FingerMageSpider);
    DomainResult<> nativeFear(const NativeFear &,uint64_t tick);
    DomainResult<std::shared_ptr<AuraUnitPublication>> prepareConsumedCorpse(EntityId,int state,uint64_t tick);
    DomainResult<std::shared_ptr<AuraUnitPublication>> prepareRedeemedCorpse(EntityId,uint64_t tick);
    DomainResult<bool> redeemUnitCorpse(EntityId source,EntityId corpse,int skill,int rank,uint64_t tick,bool skipChance=false);
    std::vector<ModifierStatSource> unitStatSources(EntityId,uint64_t tick) const;
    bool unitLifeState(EntityId,uint64_t tick) const;
    std::optional<StatSnapshot> unitStatValues(EntityId,uint64_t tick,std::optional<EntityId> weapon = {},std::shared_ptr<const StatList> mode = {}) const;
    int unitCurseResistance(EntityId,uint64_t tick) const;
    CharacterModifiers stateModifiers(EntityId,int state,uint64_t tick) const;
    int unitDefense(EntityId,uint64_t tick) const;
    int unitResistance(EntityId,DamageType,uint64_t tick,int pierce = 0) const;
    int unitRawResistance(EntityId,DamageType,uint64_t tick) const;
    FixedDamageResult unitDamageFixed(EntityId,int64_t,DamageType,uint64_t,int pierce = 0,bool bypass = false,bool sanctuary = false) const;
    FixedDamageChannelsResult unitDamageChannels(EntityId,const std::array<int64_t,6> &,uint64_t,
        const std::array<int,6> &pierce = {},bool bypass = false,bool sanctuary = false) const;
    DomainResult<> avoidance(const ActorContext &, WeaponAvoidance, EntityId attacker);
    std::set<int> unitStates(EntityId, uint64_t tick) const;
    std::map<int,std::vector<StatValue>> unitStateStats(EntityId, uint64_t tick) const;
    DomainResult<float> receive(const ActorContext &, int64_t rawDamage, DamageType);
    // Contact RNG is staged by combat; commit it only with the character hit.
    DomainResult<PlayerReceptionPlan> preparePlayerReception(const ActorContext &,EntityId,const MonsterHit &,const PlayerHitAdjustment & = {}) const;
    DomainResult<> commitPlayerReception(PlayerReceptionPlan &&);
    DomainResult<UnitReceptionPlan> prepareUnitReception(EntityId target,EntityId source,const MonsterHit &,uint64_t,
        const std::array<int,6> &pierce={},bool bypass=false,bool sanctuary=false,bool directDrain=true) const;
    DomainResult<> commitUnitReception(UnitReceptionPlan &);
    DomainResult<ReceivedDamage> receiveMonster(const ActorContext &, EntityId source, const MonsterHit &, const MonsterHitStates &,std::optional<ItemHandle> wear = {}, const PlayerHitAdjustment & = {},const DamageReception *preparedReception=nullptr,bool deferAction=false,bool successfulHit=true);
    PlayerMessageFact deathMessage(EntityId actor) const {
        const auto it=state_.players.find(actor);
        return it==state_.players.end()?PlayerMessageFact{}:PlayerMessageFact{6,{},it->second.killerName,0,0,it->second.killerClass};
    }
    DomainResult<> block(const ActorContext &);
    DomainResult<> missileReaction(EntityId target,EntityId source,bool getHit,bool knockback,uint8_t hitClass,uint64_t tick);
    DomainResult<> nativeMeleeReaction(EntityId target,EntityId source,const MonsterHit &,int64_t amount,uint64_t tick);
    bool recovering(EntityId, uint64_t tick) const;
    bool missileHitAllowed(EntityId,uint64_t tick) const;
    DomainResult<> nativeMissileHitDelay(EntityId,const CombatStateDefinition &,uint64_t duration,uint64_t tick);
    void missileHitDelay(EntityId,uint64_t until);
    void triggerMonsterCurse(EntityId,uint64_t tick);
    DomainResult<> castMonsterCurse(EntityId,const AuraDefinition &,EntityId,uint64_t tick,bool unique=false,const SkillFormulaSnapshot *source=nullptr,const MonsterCursePulse *prepared=nullptr);
    DomainResult<> monsterCurse(EntityId,int skill,int rank,Vec,uint64_t,std::shared_ptr<MonsterCursePulse> &);
    StepStatus finishContacts(uint64_t tick);
    NativeContactBatch prepareDamageEvents(EntityId source,EntityId target,int64_t physical,int64_t total,bool missile,bool returnFire,uint64_t tick,bool suppress=false) const;
    NativeContactBatch prepareAttackEvents(EntityId source,EntityId target,uint64_t tick) const;
    NativeContactBatch prepareMeleeDrainEvents(EntityId source,EntityId target,uint64_t tick) const;
    NativeContactBatch prepareThornsEvents(EntityId source,EntityId target,int64_t physical,uint64_t tick) const;
    NativeContactBatch prepareMissileEvents(EntityId source,EntityId target,uint64_t tick,bool returnFire=true) const;
    DomainResult<> advanceDamageEvents(NativeContactBatch &,uint64_t tick);
    bool hasContactEffects() const {return !monsterCurses_.empty();}
    bool reactionCapacity() const { return monsterCurses_.size()<=4096-128; }
    StepStatus step(TickContext, FrameFacts &);
};
}

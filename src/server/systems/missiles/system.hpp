#pragma once
#include "state.hpp"
#include "requests.hpp"
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/combat/requests.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "server/systems/transactions/plan.hpp"
#include "gameplay/events/frame_queue.hpp"
#include "gameplay/rules/formula.hpp"
#include "world/collision.hpp"
#include <map>
#include <functional>
#include <memory>
#include <deque>
#include <string_view>
#include <span>
namespace d2x::server {struct SkillRules;struct RoomUnitRegistration;}
namespace d2x {class SkillFormulaValues;}
namespace d2x::server::actions {class State;}
namespace d2x::server::missiles {
inline constexpr uint32_t pd2SkillMissileTimerHandler=0x5044324du;
class NativeBirthPublication;
struct Ports {
    AreaStore &areas; const PlayerStore &players; monsters::System &monsters;
    combat::System &combat; transactions::System &transactions; EntityIds &ids; uint64_t &random; EventOutbox &events; effects::System &effects;
    const spatial::System &spatial;
    const social::System &social;
    FrameEventQueue &frameEvents;
    const actions::State &actions;
    std::shared_ptr<const SkillRules> nativeRules;
    quests::System &quests;
    world::System &world;
    objects::System &objects;
    companions::System &companions;
    uint32_t &pd2Random;
};
class System {
    friend class NativeBirthPublication;
    State state_;
    const Ports ports_;
    struct Advance;
    struct Runtime;
    std::unique_ptr<Runtime> runtime_;
    DomainResult<DirectContact> nativeWeaponContact(const Spawn &,std::vector<EntityId>);
    Missile make(const Spawn &, Vec origin, Vec direction, int definition, int frames, float speed, Program, uint64_t &,bool deferSeed=false,uint64_t *ownerRandom=nullptr,
        const Missile *originSource=nullptr,uint64_t *originRandom=nullptr) const;
    using ChildSetup=std::function<void(Missile &,const SkillCastSpec &)>;
    Missile &planChild(Advance &,Spawn,Vec origin,Vec direction,int definition,int frames,float speed,Program,
        bool refreshSkill=false,ChildSetup setup={}) const;
    Missile &planMonsterChild(Advance &,Missile) const;
    DomainResult<MonsterMissileRule> sampleMonsterRule(MonsterMissileRule,EntityId,uint64_t,std::shared_ptr<const StatList> mode={},std::optional<int> assignedRank={}) const;
    DomainResult<std::vector<Missile>> buildMonster(const MonsterSpawn &,uint64_t &gameRandom,uint64_t &ownerRandom,bool &ownerDrawn) const;
    MissileRule nativeDamageRule(const SkillRules &,int definition,int rank) const;
    MissileRule nativeChildRule(const Missile &,int definition) const;
    Missile &planNativeChild(Advance &,int definition,Vec position,Vec direction,int frames=0,int loops=0) const;
    void planCollisionChild(Advance &,int radius,int period,int definition,uint16_t mask) const;
    std::unique_ptr<Advance> advanceNative(const Missile &) const;
    std::optional<uint8_t> nativeHit(Advance &,EntityId) const;
    std::optional<uint8_t> pd2Hit(Advance &,EntityId) const;
    bool preparePd2Program(Advance &) const;
    std::unique_ptr<Advance> immediateHit(const Missile &,EntityId,uint64_t tick) const;
    std::optional<int32_t> missileSkillFormula(Advance &,std::string_view) const;
    std::optional<int32_t> missileSelfFormula(Advance &,std::string_view,int skill=-1,int rank=-1) const;
    RuleNumber withMissileOwnerFormula(Advance &,const std::function<RuleNumber(SkillFormulaValues &)> &) const;
    RuleNumber withMissileOwnerRandom(Advance &,const std::function<RuleNumber(SkillFormulaValues &,uint64_t &)> &) const;
    void initializePierce(Missile &) const;
    void initializePath(Missile &) const;
    void initializeGuidance(Missile &,EntityId target,Vec point) const;
    std::shared_ptr<MissileDamageRollState> damageRolls(const Missile &) const;
    MissileDamageData prepareDamage(const Missile &,const MissileRule &,std::shared_ptr<const StatList> mode,uint64_t &ownerRandom,
        bool &ownerDrawn,const Missile *origin=nullptr,std::optional<std::reference_wrapper<uint64_t>> originRandom={},std::vector<std::pair<uint16_t,int32_t>> *typeBonuses=nullptr) const;
    std::vector<Missile> launch(const Spawn &, uint64_t &,uint64_t *ownerRandom=nullptr) const;
    Advance advance(const Missile &,TickContext) const;
    DomainStatus commitAdvance(const Missile &, Advance &, TickContext);
    StepStatus advanceFrame(EntityId,TickContext);
    struct Admission {
        FrameEventQueue::PreparedInsertion timer;
        std::shared_ptr<RoomUnitRegistration> room;
        RegionId area;Vec position;int footprint{};
    };
    std::vector<Admission> prepareTimers(const std::map<EntityId,Missile> &,bool transient=false);
    void publishTimers(std::vector<Admission> &) noexcept;
    std::vector<EntityId> nearbyUnits(const Missile &,std::optional<Vec> at={}) const;
    std::vector<EntityId> nearbyRoomUnits(const Missile &,std::optional<Vec> at={}) const;
    NativeMissileMotionResult moveMissile(Missile &) const;
    std::vector<std::pair<float,EntityId>> collisionContacts(const Missile &,Vec next) const;
    std::optional<Vec> unitPosition(const Missile &,EntityId) const;
    RegionId unitArea(EntityId) const;
    bool moveToRoom(Missile &,Vec roomAnchor) const;
    bool creationRoom(Missile &) const;
    bool townAllowed(const Missile &) const;
    combat::SpellImpact enemyImpact(Advance &,std::vector<EntityId>) const;
    Advance advanceWeapon(const Missile &) const;
    Advance advanceGroundThrow(const Missile &) const;
    void weaponImpact(Advance &, Vec) const;
    void continueMissileFlight(Advance &) const;
    bool projectileImpact(Advance &,EntityId) const;
    void areaImpact(Advance &,Vec,int,bool full=false,std::optional<uint32_t> filter={},std::optional<int32_t> length={}) const;
    combat::SpellImpact impact(Missile &, std::vector<EntityId>, std::optional<int64_t> damage = {},bool missileContact=true,
        std::shared_ptr<MissileDamageRollState> rolls={}) const;
    combat::SpellImpact impact(Advance &,std::vector<EntityId>,std::optional<int64_t> damage={},bool missileContact=true) const;
    std::vector<DomainFact> visuals(const std::vector<Missile> &) const;
    std::vector<EntityId> collisionCandidates(const Missile &, Vec next) const;
    bool canCollide(const Missile &,EntityId) const;
    std::optional<std::array<int32_t,2>> pd2NextHitKey(const Missile &) const;
    bool missileHitAllowed(const Missile &,EntityId) const;

  public:
    explicit System(Ports ports);
    ~System();
    System(const System &) = delete;
    System &operator=(const System &) = delete;
    const State &read() const { return state_; }
    std::vector<MissileFact> visible(PlayerId) const;
    DomainResult<EntityId> spawn(const Spawn &);
    DomainResult<MonsterLaunch> spawnMonster(const MonsterSpawn &);
    std::optional<int> nativeDefinition(std::string_view) const;
    DomainResult<EntityId> spawnNative(const NativeSpawn &);
    DomainResult<std::shared_ptr<NativeBirthPublication>> prepareNativeBirth(std::span<const NativeSpawn>,size_t actorPrefix=0,std::span<const DomainFact> extraFacts={},std::optional<std::pair<uint64_t,uint64_t>> gameRandom={},std::optional<std::pair<uint32_t,uint32_t>> pd2PathSeed={});
    DomainResult<std::shared_ptr<NativeBirthPublication>> preparePd2RoyalFire(const NativeSpawn &,int step);
    DomainResult<EntityId> commitNativeBirth(std::shared_ptr<NativeBirthPublication>,bool pd2ActFiveBroadcast=false);
    std::optional<combat::SpellImpact> nativeAreaContact(EntityId,bool collisionCallback=false) const;
    void finishNativeAreaContact(const combat::SpellImpact &);
    void removeTransient(EntityId);
    DomainResult<DirectContact> directContact(const Spawn &,std::vector<EntityId>);
    DomainResult<> frameEvent(EntityId,TickContext);
    DomainResult<> pd2SkillMissileEvent(const FrameEvent &,TickContext);
};
}

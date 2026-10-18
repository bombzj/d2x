#pragma once
#include "system.hpp"
#include "server/systems/effects/skill_state.hpp"
#include <functional>
#include <utility>
namespace d2x::server::effects {struct AuraUnitPublication;}
namespace d2x::server::monsters {class NativeBirthPublication;}

namespace d2x::server::missiles {
struct System::Advance {
    Missile next;
    std::vector<Missile> children;
    std::vector<combat::SpellImpact> impacts;
    bool finished{};
    std::vector<EntityId> curseTargets;
    std::vector<EntityId> buffTargets;
    enum class OperationKind { Child,Impact,Curse,Buff,ContactPrelude,ContactContinue,SourceHeal,TargetManaDrain,SkillState,OwnerSeed,Resource,TargetBurst,Fear,BaalFx,CairnPortal,ReleaseRoom,Redemption,OrbMist,BoneSegment,MissileField,NativeCollision,LevelSpawn,TowerOpen,TowerGold,TowerSound,BaalWave };
    struct BaalWave {RegionId area;Vec position;int mappedPreset{};};
    std::vector<BaalWave> baalWaves;
    struct TowerGold {RegionId area;Vec observer,position;std::optional<GroundLocation> destination;bool resolved{};};
    std::vector<TowerGold> towerGold;
    struct LevelSpawn {RegionId area;Vec observer,position;std::shared_ptr<monsters::NativeBirthPublication> publication;};
    std::vector<LevelSpawn> levelSpawns;
    struct MissileField {RegionId area;Vec position;int size{};};
    std::vector<MissileField> missileFields;
    struct BoneSegment {EntityId root;RegionId area;Vec position;};
    std::vector<BoneSegment> boneSegments;
    struct CollisionFlight {
        Vec start,end;
        std::vector<std::pair<float,EntityId>> contacts;
        size_t next{};
        bool wall{},continueFlight{true},weapon{true},piercing{},piercePrepared{},forceRemove{};
        bool nativeExpired{};
    };
    std::optional<CollisionFlight> collisionFlight;
    struct SourceHeal {EntityId owner;std::optional<int32_t> amount;};
    std::vector<SourceHeal> sourceHeals;
    struct TargetManaDrain {EntityId target;int percentage{};};
    std::vector<TargetManaDrain> manaDrains;
    struct ResourceWrite {
        enum class Kind { Life,Mana } kind{};
        EntityId target;int32_t value{};int overlay{-1};
    };
    std::vector<ResourceWrite> resources;
    struct TargetBurst {int definition{},range{},count{};uint32_t filter{};bool checkNoAura{};};
    std::vector<TargetBurst> targetBursts;
    std::vector<effects::NativeFear> fears;
    struct Redemption {
        EntityId corpse;int skill{};bool skipChance{};
        std::optional<bool> affected;
        std::shared_ptr<effects::AuraUnitPublication> publication;
    };
    std::vector<Redemption> redemptions;
    struct SkillState {
        EntityId source,target;int skill{},rank{},minimumDuration{};
        std::optional<int32_t> duration;
        std::optional<std::pair<uint64_t,uint64_t>> random;
        bool seedCommitted{};
        effects::NativeSkillStateProcedure procedure{effects::NativeSkillStateProcedure::FingerMageSpider};
        std::shared_ptr<bool> posted;
    };
    std::vector<SkillState> skillStates;
    struct Operation {OperationKind kind;size_t index;};
    std::vector<Operation> operations;
    size_t nextOperation{};
    bool started{},contactAccepted{};
    std::shared_ptr<MissileDamageRollState> damageRolls;
    struct OwnerSeedLoan {EntityId owner;uint64_t expected{},next{};};
    std::vector<OwnerSeedLoan> formulaSeeds;
    struct OriginSeedLoan {std::shared_ptr<MissileDamageRollState> state;uint64_t expected{},next{};};
    struct ChildBuildContext {
        uint64_t &gameRandom;uint64_t tick;
        std::optional<OwnerSeedLoan> ownerSeed;
        std::optional<OriginSeedLoan> originSeed;
    };
    using ChildBuilder=std::function<std::optional<Missile>(const Missile &,ChildBuildContext &)>;
    std::map<size_t,ChildBuilder> childBuilders;
    struct ChildPublication {
        std::map<EntityId,Missile> insertion;
        std::vector<Admission> timers;
        std::vector<DomainFact> facts;
        uint64_t gameRandom{};
        std::optional<OwnerSeedLoan> ownerSeed;
        std::optional<OriginSeedLoan> originSeed;
        bool identityCommitted{};
        bool published{};
        EntityId child;
        RegionId area;
        std::optional<EntityId> hitTarget;
        std::unique_ptr<Advance> immediate;
    };
    std::optional<ChildPublication> childPublication;
    void addChild(Missile value,ChildBuilder builder={}) {
        const auto index=children.size();
        if(builder) childBuilders.emplace(index,std::move(builder));
        operations.push_back({OperationKind::Child,index});children.push_back(std::move(value));
    }
    void addMissileContact(combat::SpellImpact value) {operations.push_back({OperationKind::ContactPrelude,impacts.size()});impacts.push_back(std::move(value));}
    void addImpact(combat::SpellImpact value) {operations.push_back({OperationKind::Impact,impacts.size()});impacts.push_back(std::move(value));}
    void addSkillState(EntityId source,EntityId target,int skill,int rank,int minimumDuration) {operations.push_back({OperationKind::SkillState,skillStates.size()});skillStates.push_back({source,target,skill,rank,minimumDuration});}
    void addManaDrain(EntityId target,int percentage) {operations.push_back({OperationKind::TargetManaDrain,manaDrains.size()});manaDrains.push_back({target,percentage});}
    void addSourceHeal(EntityId owner) {operations.push_back({OperationKind::SourceHeal,sourceHeals.size()});sourceHeals.push_back({owner,{}});}
    void addCurse(EntityId value) {operations.push_back({OperationKind::Curse,curseTargets.size()});curseTargets.push_back(value);}
    void addBuff(EntityId value) {operations.push_back({OperationKind::Buff,buffTargets.size()});buffTargets.push_back(value);}
};
struct System::Runtime {
    std::map<EntityId, Advance> pending;
};
}

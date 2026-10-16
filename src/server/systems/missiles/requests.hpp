#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/combat_rules.hpp"
#include "server/systems/transactions/plan.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "world/collision.hpp"
#include <cstdint>
#include <array>
#include <optional>
#include <vector>

namespace d2x::server {struct SkillRules;}
namespace d2x::server::combat {struct NativeSkillContact;}
namespace d2x::server::missiles {
struct Spawn {
    ActorContext actor;
    SkillCastSpec skill;
    MissileCollisionRule collision;
    Vec target;
    bool free{};
    EntityId emitter{};
    uint8_t emitterType{};
    std::optional<Vec> origin{};
    std::optional<WeaponSkillDamage> weapon{};
    std::optional<transactions::Plan> cost{};
    EntityId guidedTarget{};
    bool deathTrigger{};
    uint64_t directOccurrence{};
    std::optional<RegionId> directArea;
    EntityId sourceOwner; // Explicit native owner for children; emitter is presentation identity.
    std::shared_ptr<const SkillRules> preparedRules;
    std::array<int32_t,2> parameters{};
    MissilePathInitialization pathInitialization;
    std::optional<uint64_t> nativeMeleeOccurrence;
    std::shared_ptr<const MissileHitDamage> nativeMeleeBase;
    std::shared_ptr<combat::NativeSkillContact> nativeMeleeContact;
    bool nativeMeleeHeld{};
};
enum class DirectNotice { Publish,DeferPulse };
struct DirectContact {EntityId projectile;std::vector<DomainFact> deferredFacts;std::optional<uint64_t> occurrence;};
struct MonsterSpawn {
    EntityId source;
    RegionId area;
    uint64_t generation{}, tick{};
    Vec position, target;
    MonsterAttackRule attack;
    MonsterHitStates states;
    int level{}, critical{}, extraQuills{};
    std::optional<MonsterWebRule> web{};
    std::vector<Vec> aims{};
    bool postMortem{};
    bool environment{};
    std::vector<Vec> origins;
    std::vector<float> speeds;
    PlayerId playerSource;
    int nativeSkill{-1};
    std::array<int32_t,2> parameters{};
    bool directConstructor{}; // Raw CreateMissileFromParams, without a monster attack wrapper.
    bool distanceCountdown{}; // Native constructor flag 0x400.
    std::optional<int> startFrame; // Native constructor flag 0x200, including an explicit zero.
    std::optional<int> totalFrames; // Native 0x8000 range override, applied before path initialization.
};
struct MonsterLaunch { EntityId first; };
struct NativeSpawn {
    EntityId owner;
    RegionId area;
    uint64_t generation{},tick{};
    Vec position,target;
    int definition{-1},skill{-1},rank{1};
    std::array<int32_t,2> parameters{};
    bool distanceCountdown{};
    std::optional<int> startFrame;
    // SkillAss::sub_6FCF8330 calls InitLowSeed (high word 666) once, then
    // interleaves coordinate draws with each missile's native constructor.
    std::optional<uint32_t> ownerLowSeed;
    int32_t scatterRadius{};
    int32_t minimumTargetDistanceSquared{};
    std::optional<std::pair<uint64_t,uint64_t>> ownerSeedEdit;
    enum class Initialization { None,ProgressiveCharged,ProgressiveSpiral,ChargedSkill } initialization{};
    uint32_t initialSeed{};
    std::optional<int32_t> nativeVelocity;
    std::optional<std::array<int32_t,2>> finalParameters;
    int initializationIndex{};
    std::optional<int> totalFrames;
    std::optional<EntityId> storedOwner;
    bool copyMonsterAttackBonus{}; // SKILLS_CreateMissile, as distinct from raw params.
};
}

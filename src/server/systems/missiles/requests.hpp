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
    std::optional<RegionId> directArea{};
    EntityId sourceOwner{}; // Explicit native owner for children; emitter is presentation identity.
    std::shared_ptr<const SkillRules> preparedRules{};
    std::array<int32_t,2> parameters{};
    MissilePathInitialization pathInitialization{};
    std::optional<uint64_t> nativeMeleeOccurrence{};
    std::shared_ptr<const MissileHitDamage> nativeMeleeBase{};
    std::shared_ptr<combat::NativeSkillContact> nativeMeleeContact{};
    bool nativeMeleeHeld{};
};
struct DirectContact {EntityId projectile;std::optional<uint64_t> occurrence;};
struct MonsterSpawn {
    EntityId source;
    RegionId area;
    uint64_t generation{}, tick{};
    Vec position, target;
    MonsterAttackRule attack;
    MonsterHitStates states;
    int level{}, critical{}, extraQuills{};
    std::vector<Vec> aims{};
    bool postMortem{};
    bool environment{};
    std::vector<Vec> origins{};
    std::vector<float> speeds{};
    PlayerId playerSource{};
    int nativeSkill{-1};
    std::array<int32_t,2> parameters{};
    bool directConstructor{}; // Raw CreateMissileFromParams, without a monster attack wrapper.
    bool distanceCountdown{}; // Native constructor flag 0x400.
    std::optional<int> startFrame{}; // Native constructor flag 0x200, including an explicit zero.
    std::optional<int> totalFrames{}; // Native 0x8000 range override, applied before path initialization.
    std::optional<EntityId> guidedTarget{};
    std::optional<int32_t> nativeVelocityRaw{}; // Constructor's 8.8 value before slow / 75% scaling.
    int32_t initialDamagePercent{};
    std::optional<WeaponSkillDamage> weapon{};
};
struct MonsterLaunch { EntityId first; };
struct NativeSpawn {
    struct DamageAdds {
        int32_t minimumPhysical{},maximumPhysical{},minimumPoison{},maximumPoison{},poisonLength{},poisonCount{};
    };
    EntityId owner;
    RegionId area;
    uint64_t generation{},tick{};
    Vec position,target;
    int definition{-1},skill{-1},rank{1};
    std::array<int32_t,2> parameters{};
    bool distanceCountdown{};
    std::optional<int> startFrame{};
    // SkillAss::sub_6FCF8330 calls InitLowSeed (high word 666) once, then
    // interleaves coordinate draws with each missile's native constructor.
    std::optional<uint32_t> ownerLowSeed{};
    int32_t scatterRadius{};
    int32_t minimumTargetDistanceSquared{};
    std::optional<std::pair<uint64_t,uint64_t>> ownerSeedEdit{};
    enum class Initialization { None,ProgressiveCharged,ProgressiveSpiral,ChargedSkill,DiabWall,BlessedHammer,Pd2Firestorm } initialization{};
    uint32_t initialSeed{};
    std::optional<int32_t> nativeVelocity{};
    std::optional<std::array<int32_t,2>> finalParameters{};
    std::optional<std::array<int32_t,2>> firstFinalParameters{}; // Post-wrapper edit of its returned missile only.
    int initializationIndex{};
    std::optional<int> totalFrames{};
    std::optional<EntityId> storedOwner{};
    bool copyMonsterAttackBonus{}; // SKILLS_CreateMissile, as distinct from raw params.
    bool transient{}; // SrvDo029 owns a real temporary missile until its direct callback finishes.
    std::optional<EntityId> guidedTarget{};
    std::optional<int> postCreationFrames{}; // SetTotal/Current after construction; preserve Activate.
    bool velocityFixed{}; // Flag0x10: nativeVelocity already has eight fractional bits.
    std::optional<int> activateFrame{}; // Constructor flag0x800, independent of Range.
    std::optional<int> attackBonus{}; // Post-constructor STAT_TOHIT write.
    bool suppressSourceEvents{}; // Constructor flag0x10000 (outer multishot projectiles).
    bool pd2SharedClassHitDelay{}; // Unit flag0x80000: NextHit key excludes a player owner's GUID.
    int32_t initialDamagePercent{}; // InitFunc stat write before SetDamageStats.
    std::optional<WeaponSkillDamage> weapon{};
    std::optional<DamageAdds> damageAdds{}; // Current PD2 0x36CD50, after SetDamageStats.
};
}

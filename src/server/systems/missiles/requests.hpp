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
};
enum class DirectNotice { Publish,DeferPulse };
struct DirectContact {EntityId projectile;std::vector<DomainFact> deferredFacts;};
struct MonsterSpawn {
    EntityId source;
    RegionId area;
    uint64_t generation{}, tick{};
    Vec position, target;
    MonsterAttackRule attack;
    MonsterHitStates states;
    int level{}, critical{}, extraQuills{};
    uint64_t random{};
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
};
struct MonsterLaunch { EntityId first; uint64_t random{}; };
struct NativeSpawn {
    EntityId owner;
    RegionId area;
    uint64_t generation{},tick{};
    Vec position,target;
    int definition{-1},skill{-1},rank{1};
    std::array<int32_t,2> parameters{};
    bool distanceCountdown{};
    std::optional<int> startFrame;
};
}

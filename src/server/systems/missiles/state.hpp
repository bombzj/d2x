#pragma once
#include "server/game_messages.hpp"
#include "world/identity.hpp"
#include "server/systems/combat/requests.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/missiles/damage.hpp"
#include "gameplay/missiles/damage_data.hpp"
#include "gameplay/missiles/motion.hpp"
#include "gameplay/missiles/timing.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <cstdint>
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <set>

namespace d2x::server {struct SkillRules;}
namespace d2x::server::missiles {
struct EnemyProjectile { MonsterMissileRule rule; MonsterHit hit; MonsterHitStates states; int level{}, rating{};std::optional<WeaponSkillDamage> weapon{};int criticalChance{}; };
// Numeric missile sources are independent of trajectory/skill programs.
struct NativeDamage {
    MissileRule rule;
    MissileDamageData data;
    std::vector<std::pair<uint16_t,int32_t>> monsterTypeBonuses;
    int attackBonus{};
};
enum class Program { Projectile, Ring, Charged, Orb, OrbBolt, OrbNova, Blizzard, Shard, Arc, FirewallMaker, Fire, Meteor, PoisonCloud, AreaImpact, FuryBolt, GroundThrow, Hammer, Heaven, Spirit, GrimWard };
struct Missile {
    EntityId id, owner;
    PlayerId player;
    EntityId playerActor;
    std::shared_ptr<const SkillRules> rules;
    int definition{};
    RegionId area;
    uint64_t generation{}, created{}, expires{}, revision{};
    uint64_t executionTick{}; // Actual callback frame; independent of native countdown and chronological path age.
    Vec position, velocity;
    float acceleration{}, maximumVelocity{}, radius{};
    MissileCollisionRule collision;
    int64_t damage{};
    std::optional<combat::SpellImpact> impact;
    SkillCastSpec skill;
    Program program{Program::Projectile};
    uint64_t random{};
    int ageFrames{}, lifetimeFrames{}, directionIndex{}, remainingHits{};
    int nativeTotalFrames{},nativeActivateFrame{},nativeSequenceFrame{};
    // Chronological age is separate from the native signed countdown.
    // ResetCurrentFrame never changes TotalFrames; GetRemainingFrames is
    // the native difference total-current, not the countdown itself.
    int currentFrame() const {return nativeSubtract(lifetimeFrames,ageFrames);}
    int elapsedFrames() const {return nativeSubtract(nativeTotalFrames,currentFrame());}
    void setCurrentFrame(int frames) {
        lifetimeFrames=nativeAdd(ageFrames,nativeMissileFrameCount(frames));
        expires=created+uint64_t(std::max(lifetimeFrames,0));
    }
    void initializeFrames(int frames,int activation=0,std::optional<int> start={}) {
        const auto initial=nativeMissileInitialFrames(frames,activation,start);
        nativeActivateFrame=initial.activate;nativeTotalFrames=initial.total;
        nativeSequenceFrame=initial.sequence;setCurrentFrame(initial.current);
    }
    std::array<int32_t,2> parameters{}; // Native TargetX/TargetY initialization slots.
    MissilePathInitialization pathInitialization;
    EntityId lastHit, emitter; uint8_t emitterType{};
    Vec turnTarget;
    Vec pathTarget;
    bool movedCell{};
    NativeMissileMotion motion;
    uint64_t pathRevision{};
    std::optional<EntityId> hitOnCreation;
    std::deque<Vec> path;
    std::optional<WeaponSkillDamage> weapon{};
    EntityId guidance{};
    int pierces{};
    int pierceCount{};
    std::set<EntityId> weaponContacts{};
    bool guidanceSearched{};
    std::optional<EnemyProjectile> enemy;
    CombatSourceBinding binding{};
    bool environment{},suppressSourceEvents{},creationRejected{},nativeProgram{};
    bool pd2SharedClassHitDelay{};
    std::optional<MissileDamageRange> damageRange;
    std::optional<NativeDamage> nativeDamage;
    std::array<int32_t,2> nativeResources{}; // Native missile HITPOINTS/MANA, distinct from its owner's resources.
    int32_t initialDamagePercent{};
};
struct State { std::map<EntityId, Missile> missiles; };
}

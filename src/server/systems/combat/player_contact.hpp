#pragma once
#include "requests.hpp"
#include "weapon_hit.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "gameplay/combat/weapon_hit.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/combat/drain.hpp"
#include "server/systems/monsters/damage_status.hpp"

namespace d2x::server::combat {
struct MissileContactExecution {
    size_t targetIndex{};
    bool accuracyPrepared{},hit{},damagePrepared{},defensePrepared{},blocked{},criticalPrepared{},reactionPrepared{};
    WeaponAvoidance avoidance{WeaponAvoidance::None};
    MonsterHit damage;
    NativeDrainResult drain;
    bool drainPrepared{},drainCommitted{};
    bool delayPrepared{};
    bool damageProgramDomainPrepared{};
    std::optional<std::pair<uint64_t,uint64_t>> damageFormulaSeed;
    bool flagsPrepared{};
    bool resultSuccessful{true};
    bool proceduresPrepared{},armorCommitted{};
    bool weaponWearCommitted{},defenderWearCommitted{};
    WeaponHitEffects extras;
    WeaponHitResult playerExtras;
    reactions::ItemEventPlan itemEvents;
    uint64_t drainExpected{},drainRandom{};
    int drainOverlay{-1};
};
// A single accepted contact owns calculation, pre-HP callbacks and HP commit.
// Its cursor stays with the impact, never with a global deferred reaction list.
struct PlayerContactExecution {
    size_t targetIndex{};
    ActorContext victim,attacker;
    EntityId target;
    MonsterHit hit;
    MonsterHitStates states;
    effects::PlayerHitAdjustment adjustment;
    effects::PlayerReceptionPlan defense;
    effects::NativeContactBatch callbacks;
    std::optional<ItemHandle> wear;
    std::optional<WeaponSkillDamage> leech;
    WeaponHitResult extras;
    bool nativeMissile{};
    reactions::ItemEventPlan itemEvents;
    uint64_t random{};
    uint32_t hitClass{};
    int64_t remainingLife{};
    bool damageCommitted{};int64_t receivedPhysical{};
    bool defensePrepared{},defenseCommitted{},callbacksPrepared{},callbacksComplete{},missile{},hostile{},fillEventComplete{},meleeEventComplete{};
};
struct MonsterContactExecution {
    size_t targetIndex{};
    EntityId target;
    std::array<int64_t,6> channels{};
    std::array<int,6> pierce{};
    int64_t lifeDrain{};
    int64_t manaDrain{},staminaDrain{};
    bool nativeMissile{},bypass{};
    monsters::DamageStatus statuses;
    std::optional<bool> getHit;
    effects::NativeContactBatch callbacks;
    std::optional<effects::UnitReceptionPlan> defense;
    int64_t amount{},absorbed{},physical{},recoil{};
    uint64_t cold{},stun{},freeze{};
    std::optional<PoisonApplication> poison;
    WeaponHitEffects extras;
    std::optional<WeaponSkillDamage> leech;
    LeechDivisors divisors;
    reactions::ItemEventPlan itemEvents;
    uint64_t random{};
    int leechOverlay{-1};
    uint8_t hitClass{};
    bool receptionPrepared{},defenseCommitted{},callbacksPrepared{},callbacksComplete{},convert{},hostile{},clearStun{},gainsPrepared{},sanctuary{},fillEventComplete{},meleeEventComplete{},damageCommitted{};
};
}

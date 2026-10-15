#pragma once
#include "world/identity.hpp"
#include "gameplay/combat/source.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/monsters/damage.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include "gameplay/missiles/damage_data.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace d2x::server::effects {struct NativeContactBatch;struct UnitReceptionPlan;}
namespace d2x::server::combat {
struct PlayerContactExecution;
struct MonsterContactExecution;
struct MissileContactExecution;
struct Damage {
    EntityId source, target; DamageType type; int64_t minimum{}, maximum{}; uint64_t action{};
    RegionId area; uint64_t impact{}; int rating{}, level{}, range{}, sourceSize{2};
    std::optional<WeaponDamage> weapon;
    int criticalChance{}, deadlyChance{};
    bool reactionsStarted{}, cancelled{};
    std::optional<CombatModifiers> attackModifiers{};
    uint8_t monsterMode{4};
    uint64_t sourceInterruption{};
    std::optional<uint64_t> actionRandom{};
    CombatSourceBinding binding{};
    std::optional<SkillCastSpec> skill{};
    std::optional<MonsterAttackRule> monsterSkill{};
};
enum class MissilePreludeResult { Ignore,Miss,Hit };
enum class ImpactPhase { Contact, FollowUp, Recoil };
struct MissileAreaRoll {std::optional<MissileHitDamage> value;};
struct MissileAreaDamage {
    MissileDamageElement element{};bool full{};std::optional<int32_t> length;
    std::shared_ptr<MissileAreaRoll> rolled;
    uint32_t hitFlags{};
    uint16_t resultFlags{};
};
struct SpellImpact {
    EntityId projectile, source;
    RegionId area;
    DamageType type{DamageType::Fire};
    int64_t damage{};
    std::vector<EntityId> targets;
    size_t next{};
    uint64_t occurrence{}, coldFrames{}, nextDelay{};
    bool freeze{}, knockback{};
    bool returnFire{}, reaction{};
    int coldPierce{}, poisonPierce{};
    std::array<int,3> staticFloors{};
    int staticPercent{}, staticMinimum{};
    int64_t minimumStaticDamage{};
    std::vector<int64_t> targetDamage{};
    uint8_t hitClass{};
    std::optional<WeaponSkillDamage> weapon{};
    uint64_t poisonFrames{};
    uint64_t stunFrames{};
    std::optional<bool> weaponHit{};
    std::optional<MonsterHit> monsterHit{};
    std::shared_ptr<MissileDamageRollState> missileDamage;
    bool nativeDamageFlagsOnly{}; // Direct FillDamageParams -> SetDamageFlags, without collision callbacks/SrvDmg.
    std::optional<std::pair<size_t,MissileHitDamage>> missileRolled;
    // SrvHit area programs copy one GetDamageValue result to every target.
    std::optional<MissileAreaDamage> areaDamage;
    int missileCriticalChance{};
    bool missileCriticalCommitted{};
    std::shared_ptr<MissileContactExecution> missileContact;
    std::optional<CombatStateDefinition> nativeNextHit;
    MonsterHitStates monsterStates{};
    int monsterRating{}, monsterLevel{};
    bool monsterToHit{};
    int64_t sourceHeal{};
    std::optional<uint64_t> contactRandom{};
    bool unblockable{};
    ImpactPhase phase = ImpactPhase::Contact;
    int64_t selfDamage{};
    int64_t healing{}; bool undeadOnly{};
    bool allocatedMissile{},collideFriend{},healingContact{},healingComputed{},healingCommitted{};
    std::shared_ptr<MissileDamageRollState> healingRolls;
    std::optional<WeaponSkillSpec> conversion{};
    int healingOverlay{-1};
    CombatSourceBinding binding{};
    size_t plannedTarget{};
    std::optional<uint64_t> targetRandom{};
    bool wearApplied{};
    bool frenzyApplied{};
    int64_t lifeGain{}, manaGain{};
    int contactOverlay{-1}, leechOverlay{-1};
    EntityId meleeReactionTarget{};
    bool environment{}, directLife{};
    std::shared_ptr<effects::UnitReceptionPlan> environmentDefense;
    bool environmentDefenseCommitted{};
    bool synchronous{}; // A callback owns this contact until HP/follow-ups finish.
    bool nativeAura{}; // Target room may be in an adjacent level.
    uint64_t environmentGeneration{};
    bool softHit{};
    std::shared_ptr<PlayerContactExecution> playerContact;
    std::shared_ptr<MonsterContactExecution> monsterContact;
    std::shared_ptr<effects::NativeContactBatch> attempt;
    bool melee{},damageScaled{},suppressSourceEvents{};
    unsigned pendingAttempts{};
    bool attemptOnly{};
    std::optional<size_t> missileEventTarget;
    std::shared_ptr<effects::NativeContactBatch> missileEvents;
    int64_t currentDamage() const { return targetDamage.empty() ? damage : targetDamage.at(next); }
};
struct SpellPlan { std::list<SpellImpact> spells; size_t targets{}; };
}

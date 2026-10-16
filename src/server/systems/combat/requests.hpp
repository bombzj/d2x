#pragma once
#include "world/identity.hpp"
#include "gameplay/combat/source.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/monsters/damage.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/combat/avoidance.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include "gameplay/missiles/damage_data.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace d2x::server::effects {struct NativeContactBatch;struct UnitReceptionPlan;}
namespace d2x::server::reactions {class ItemEventPlan;}
namespace d2x::server::combat {
struct PlayerContactExecution;
struct MonsterContactExecution;
struct MissileContactExecution;
struct ProgressiveContactExecution;
struct WeaponBlockCursor {EntityId target;bool checked{},blocked{},reactionComplete{};};
struct DirectDefenseCursor {EntityId target;bool accuracyPrepared{},hit{true},prepared{},reactionComplete{};NativeBlockOrDodge result;};
enum class MissilePreludeResult { Ignore,Miss,Hit };
enum class ImpactPhase { Contact, FollowUp };
struct MissileAreaRoll {std::optional<MissileHitDamage> value;};
struct MissileAreaDamage {
    MissileDamageElement element{};bool full{};std::optional<int32_t> length;
    std::shared_ptr<MissileAreaRoll> rolled;
    uint32_t hitFlags{};
    uint16_t resultFlags{};
};
// Direct skill callbacks have an owner-seeded roll and can execute their
// damage events before choosing result flags (SkillAss::SrvDo033).
struct NativeSkillContact {
    int skill{},rank{};
    std::optional<MonsterAttackRule> monsterProgram;
    int vengeanceCycle{};
    bool vengeanceCyclePosted{},vengeanceEquipmentFilled{};
    bool psychicHammer{},rolled{},flagsComplete{};
    bool accuracyOnly{},monsterCritical{},monsterCriticalPosted{};
    std::optional<int> sourceDamage;
    bool melee{},progressive{},accuracyPrepared{},accuracyHit{},defensePrepared{},hit{true},reactionPrepared{},attemptComplete{},chargePosted{};
    NativeBlockOrDodge defense;
    std::shared_ptr<effects::NativeContactBatch> attempt;
    std::shared_ptr<effects::NativeContactBatch> drainEvents,thornsEvents;
    bool drainEventsComplete{},thornsComplete{};
    std::shared_ptr<reactions::ItemEventPlan> attackItems;
    bool attackItemsComplete{};
    bool proceduresTransferred{};
    bool physicalCapped{};
    bool actionComplete{};
    bool drainRangeChecked{},skipMissDrain{};
    std::shared_ptr<ProgressiveContactExecution> release;
    bool progressiveReleaseChecked{},progressiveReleaseSuppressed{};
    bool overlayPrepared{},targetArmorPrepared{},programPrefilled{},postAllocationComplete{};
    bool modeRatePrepared{};
    bool terminalOverlayComplete{};
    bool monsterProfilePrepared{};
    uint64_t modeRevision{};
    std::shared_ptr<const MissileHitDamage> initialDamage;
    bool kick{},kickKnockback{},heldPrepared{},cancelled{},fillEventComplete{},elementsComplete{};
    int32_t allocatedPhysical{};
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
    std::optional<MonsterHit> monsterHit{};
    std::shared_ptr<MissileDamageRollState> missileDamage;
    bool nativeDamageFlagsOnly{}; // Direct FillDamageParams -> SetDamageFlags, without collision callbacks/SrvDmg.
    bool nativeDrainHeld{}; // AllocCombat has run; DrainItemDurability is a later action event.
    std::optional<std::pair<size_t,MissileHitDamage>> missileRolled;
    // SrvHit area programs copy one GetDamageValue result to every target.
    std::optional<MissileAreaDamage> areaDamage;
    std::shared_ptr<NativeSkillContact> nativeSkill;
    int missileCriticalChance{};
    bool missileCriticalCommitted{};
    std::shared_ptr<MissileContactExecution> missileContact;
    std::optional<CombatStateDefinition> nativeNextHit;
    MonsterHitStates monsterStates{};
    int monsterRating{}, monsterLevel{};
    bool monsterToHit{};
    int64_t sourceHeal{};
    bool unblockable{};
    ImpactPhase phase = ImpactPhase::Contact;
    int64_t healing{}; bool undeadOnly{};
    bool allocatedMissile{},collideFriend{},healingContact{},healingComputed{},healingCommitted{};
    std::shared_ptr<MissileDamageRollState> healingRolls;
    int healingOverlay{-1};
    CombatSourceBinding binding{};
    int64_t lifeGain{}, manaGain{};
    int contactOverlay{-1}, leechOverlay{-1};
    EntityId meleeReactionTarget{};
    bool environment{}, directLife{};
    std::shared_ptr<effects::UnitReceptionPlan> environmentDefense;
    bool environmentDefenseCommitted{};
    bool synchronous{}; // A callback owns this contact until HP/follow-ups finish.
    bool nativeAura{}; // Target room may be in an adjacent level.
    bool retainDeadSource{}; // An entered native callback finishes its finder walk after source death.
    uint64_t environmentGeneration{};
    bool softHit{};
    std::shared_ptr<PlayerContactExecution> playerContact;
    std::shared_ptr<MonsterContactExecution> monsterContact;
    bool melee{},damageScaled{},suppressSourceEvents{};
    WeaponBlockCursor weaponBlock;
    DirectDefenseCursor directDefense;
    std::optional<size_t> missileEventTarget;
    std::shared_ptr<effects::NativeContactBatch> missileEvents;
    int64_t currentDamage() const { return targetDamage.empty() ? damage : targetDamage.at(next); }
};
struct SpellPlan { std::list<SpellImpact> spells; size_t targets{}; };
}

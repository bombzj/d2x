#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/missiles/definition.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/monsters/ai_spec.hpp"
#include "gameplay/monsters/combat_values.hpp"
#include "gameplay/monsters/damage.hpp"
#include "gameplay/monsters/mode_stats.hpp"
#include "gameplay/monsters/life_stats.hpp"
#include "gameplay/monsters/unique_modifiers.hpp"
#include "gameplay/skills/firewall_spec.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/aura_spec.hpp"
#include "gameplay/items/property_program.hpp"
#include "core/math.hpp"
#include "world/collision.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace d2x {struct StatRuleCatalog;struct SkillFormulaCatalog;}
namespace d2x::server {
struct MonsterSpawnParameters {int nativeClass{-1};uint8_t nativeMode{1};int birthTicks{};};
enum class MinionBirthProcedure {Spawner, BaalTentacle, BaalClone};
struct PreparedMonster;
// Value-only admission contracts; archive/table lookups belong to hosting.
struct AttackAnimation { int frames{}, speed{}, actionFrame{}, startFrame{}; std::vector<uint8_t> events; };
using MonsterMissileRule = MissileRule;
// Native monster modes are distinct from Skills.txt identities. A1/A2 keep
// their own release frame, accuracy and damage even when no skill is assigned.
struct MonsterAttackRule {
    int minimum{}, maximum{}, rating{}, duration{}, release{};
    std::vector<MonsterElementAttack> elements;
    std::optional<MonsterMissileRule> missile{};
    enum class Action { Damage, Resurrect, Nest, Web, Spray, Firewall, Teleport, Trap, Skill, Pd2Projectile,
                        Heal, BurrowUp, BurrowDown, Lay, Hatch, Charge, Leap, SwarmMove, Nova, Curse, Animation, Assassin, Druid, Inferno, Mosquito, Submerge, GroundMissile, Hydra, NativeMissile, EatCorpse, DefensiveBuff, Impregnate, Rally, Whip, SupportMissile, SpawnMinion, NativeSpell, DiabloRun, Whirlwind, LeapAttack } action{Action::Damage};
    uint8_t nativeMode{4}, rank{1};
    std::optional<MonsterMissileRule> extraQuill{};
    std::vector<int> releaseFrames{};
    std::optional<MonsterMissileRule> groundFire{};
    std::optional<SkillCastSpec> weaponSkill{};
    bool casting{};
    int movementStart{};
    bool knockback{}, alwaysHit{};
    int hitClass{-1};
    std::optional<AnimationTiming> animation{};
    int animationAction{-1};
    bool animationSequence{};
    int submergeHoldFrame{-1}; // MonSeq clock point whose displayed frame is zero.
    uint8_t animationEvent{1};
    std::vector<int> animationEvents{};
    std::vector<uint8_t> animationFlags{};
    bool useAttackRate{true};
    int animationStartFrame{};
    std::shared_ptr<const StatList> modeStats{};
    MonsterModePhysical ownerPhysical{};
    int infernoFrame{},infernoEndFrames{};
};
struct MonsterWebRule {CombatStateDefinition aura;};
struct MonsterRule {
    struct SelfResurrection {int chance{},delay{},ticks{};};
    std::optional<SelfResurrection> selfResurrection;
    std::shared_ptr<const CombatStateRules> states;
    std::shared_ptr<const StatRuleCatalog> stats;
    std::shared_ptr<const SkillFormulaCatalog> formulas;
    std::array<std::shared_ptr<const MonsterModeTable>,3> modeTables;
    std::array<std::shared_ptr<const MonsterLifeTable>,3> lifeTables;
    int ownerDamageSkill{-1};
    int fingerMageCurseState{-1};
    int boneArmorState{-1};
    int pregnantState{-1},painwormBaseClass{-1},impBaseClass{-1};
    int attachedState{-1},siegeBeastBaseClass{-1},barricadeTowerBaseClass{-1};
    int storedOwnerState{-1};
    int bloodlustState{-1},changeClassState{-1},shortParameterStat{-1};
    std::map<int,int> whipClasses;
    bool deathKillsStoredOwner{};
    int monsterChainOffset{};
    std::array<std::array<int,8>,4> impParameters{};
    std::array<uint16_t,8> impBaseSkills{};
    std::array<uint8_t,8> impBaseModes{};
    int closeCurseState{-1},doomKnightBaseClass{-1};
    std::map<int,int> baseSkillRanks;
    std::vector<std::pair<int,int>> initialSkills;
    struct BirthProperty {
        uint8_t chance{};int parameter{},minimum{},maximum{};
        std::vector<PropertyInstruction> program;
        std::optional<PropertySkillLevels> skill;
    };
    std::vector<BirthProperty> birthProperties;
    struct BirthEquipment {
        struct Choice {std::string code;uint8_t body{},quality{};};
        uint16_t level{};std::vector<Choice> choices;
    };
    std::vector<BirthEquipment> birthEquipment;
    int statGameType{1}; // Native nGameType || dwGameType, not the expansion flag.
    int nativeClass{}, level{}, minimumLife{}, maximumLife{}, defense{}, attackRating{};
    int corpseExplosionPercent{};
    bool boneDeathExplosion{};
    int minimumDamage{}, maximumDamage{}, criticalChance{};
    std::array<int, 6> resistances{};
    int size{}, meleeRange{}, attackTicks{}, impactTick{}, deathTicks{}, decisionTicks{};
    int neutralDelayTicks{15};
    uint16_t immediateAiModes{}; // SplEndGeneric exceptions; bits are native modes.
    int nativeVelocity{}, difficulty{};
    int alignment{}, alignmentState{-1}, alignmentStat{-1};
    MovementCollisionRule collision;
    MovementCollisionRule spawnCollision;
    bool nativeSpawnFloor{};
    std::optional<std::pair<Vec,MovementCollisionRule>> spawnAuxiliary;
    bool spawnAuxiliaryOnAdmissionOnly{};
    NativeCollisionPattern collisionPattern{NativeCollisionPattern::None};
    bool demon{}, undead{}, boss{}, lesserUndead{};
    int stunState{-1};
    bool convertible{};
    bool curseable{},switchAi{},revivable{};
    bool curseAiRunMode{},curseAiInteract{};
    bool wakeAiOutsideNeutral{};
    int nativeRunVelocity{};
    NativeMonsterStateTarget stateTarget;
    bool noAura{};
    int petType{},petVariant{-1};
    bool primeEvil{};
    bool environmentController{};
    bool neverCount{};
    bool flying{};
    enum class Trap { None, Missile, Poison, Nova, RightArrow, LeftArrow, Melee, Turret, Soul } trap{};
    std::array<int,8> trapParameters{};
    struct ModifierChoices {int championChance{};std::array<std::vector<MonsterModifierChoice>,2> candidates;};
    std::shared_ptr<const ModifierChoices> modifierChoices;
    bool shrineEligible{};
    bool noTreasure{};
    bool noExperience{};
    int coldEffect{}, coldState{-1}, uninterruptableState{-1};
    CombatStateDefinition frozenState, shatterState,burningState;
    int fireShrineState{-1};
    int healthPotionState{-1},deathDelayState{-1};
    bool deadCollision{};
    int restore{};
    int difficultyResistancePenalty{};
    int knockbackTicks{};
    std::vector<uint64_t> experience;
    uint64_t experienceBase{};
    int experienceLevel{},experienceShift{};
    std::vector<int> experienceRatios;
    MonsterAiProfile ai{};
    std::map<uint8_t, MonsterAttackRule> attacks;
    MonsterHitStates hitStates;
    int damageRegen{};
    int drain{},openWoundsState{-1};
    int hirelingBossDamagePercent{100},hirelingRegeneration{};
    std::map<uint16_t,MonsterAttackRule> skillActions;
    std::array<uint16_t,8> skillIds{};
    std::shared_ptr<const PreparedMonster> nestChild;
    std::shared_ptr<const PreparedMonster> deathReinitialize;
    std::vector<int> resurrectionClasses;
    Vec spawnOffset;
    uint8_t spawnMode{1};
    int spawnFallbackClass{-1};
    int uberMinionState{-1};
    std::array<int,4> baalTentacleClasses{{-1,-1,-1,-1}};
    int spawnTicks{}, skillMoveState{-1};
    int baseClass{}, blueState{-1}, redState{-1};
    std::map<uint8_t,int> aiPoseTicks{};
    int summonBirthTicks{},summonLifeFrames{};
    std::optional<AuraDefinition> nativeAura{};
    std::shared_ptr<const std::map<int,AuraSkillSpec>> auraPrograms;
    int noManaRegenState=-1;
    std::optional<int> hitBurst,deathBurst;
    std::optional<MonsterWebRule> web;
    bool corpseSelectable{}, softCorpse{}, opensDoors{};
    int corpseSize = 1;
    int corpseNoSelectState{-1};
    int redeemedState{-1};
    int resurrectionTicks{}, threat{};
    uint8_t resurrectionMode{1};
    int hitRecoveryTicks{}, blockTicks{}, blockChance{}, coldDivisor{1}, freezeDivisor{1};
    std::optional<AnimationTiming> hitRecoveryAnimation, blockAnimation;
    bool blockWithoutShield{};
    bool nativeBlockException{};
    std::array<uint8_t,16> componentCounts{};
    uint8_t totalPieces{};
    uint16_t sharedRandomComponents{}; // One native &3 draw, shared by these component slots.
    bool skipRegionComponents{};
    uint8_t hitClass{};
    std::vector<bool> shieldChoices;
    std::optional<MonsterEnchantment> enchantment{};
    std::vector<int> nativeModifiers;
    bool bossTransfer{};
    int superUniqueIndex{-1}, uniqueTranslation{-1};
    std::optional<MonsterFirewall> firewall;
    struct AuraStats { std::vector<StatValue> targets,owner; };
    std::map<int,AuraStats> auraStats;
    bool introduction{};
    int preventHealState{-1};
    struct DeathSweep {int radius{},minimumDelay{},maximumDelay{};bool undeadOnly{};};
    std::optional<DeathSweep> deathSweep;
};
struct PreparedMonster {
    MonsterIdentity identity{};
    std::optional<MonsterKind> implementation{};
    Vec position;
    MonsterRule rule;
    std::vector<Vec> skillPositions{};
};
}

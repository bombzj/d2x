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
#include "world/navigation.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace d2x {struct StatRuleCatalog;struct SkillFormulaCatalog;}
namespace d2x::server {
struct PreparedMonster;
// Value-only admission contracts; archive/table lookups belong to hosting.
struct AttackAnimation { int frames{}, speed{}, actionFrame{}, startFrame{}; };
struct MeleeRules { std::map<std::string, AttackAnimation, std::less<>> animations; };
using MonsterMissileRule = MissileRule;
// Native monster modes are distinct from Skills.txt identities. A1/A2 keep
// their own release frame, accuracy and damage even when no skill is assigned.
struct MonsterAttackRule {
    int minimum{}, maximum{}, rating{}, duration{}, release{};
    std::vector<MonsterElementAttack> elements;
    std::optional<MonsterMissileRule> missile{};
    enum class Action { Damage, Resurrect, Nest, Web, Spray, Firewall, Teleport, Trap, Skill,
                        Heal, BurrowUp, BurrowDown, Lay, Hatch, Charge, Leap, SwarmMove, Nova, Curse, Animation, Assassin, Druid } action{Action::Damage};
    uint8_t nativeMode{4}, rank{1};
    std::optional<MonsterMissileRule> extraQuill{};
    std::vector<int> releaseFrames{};
    std::optional<MonsterMissileRule> groundFire{};
    std::optional<SkillCastSpec> weaponSkill{};
    std::optional<SkillCastSpec> spell{};
    bool casting{};
    int damagePercent{}, ratingPercent{}, stunFrames{}, healMinimum{}, healMaximum{}, hatchCount{};
    int movementPercent{100}, movementStart{};
    bool knockback{}, alwaysHit{};
    int hitClass{-1};
    std::optional<AuraDefinition> curse{};
    std::optional<AnimationTiming> animation{};
    int animationAction{-1};
    bool animationSequence{};
    std::vector<int> animationEvents{};
    bool useAttackRate{true};
    int animationStartFrame{};
    std::shared_ptr<const StatList> modeStats;
};
struct MonsterWebRule {
    int missile{-1}, frames{}, auraFrames{}, slowFrames{}, slowPercent{};
    float radius{};
    CombatStateDefinition aura, slow;
    MonsterMissileRule lay{}, ground{};
};
struct MonsterRule {
    std::shared_ptr<const CombatStateRules> states;
    std::shared_ptr<const StatRuleCatalog> stats;
    std::shared_ptr<const SkillFormulaCatalog> formulas;
    std::array<std::shared_ptr<const MonsterModeTable>,3> modeTables;
    std::array<std::shared_ptr<const MonsterLifeTable>,3> lifeTables;
    int ownerDamageSkill{-1};
    std::map<int,int> baseSkillRanks;
    std::vector<std::pair<int,int>> initialSkills;
    int statGameType{1}; // Native nGameType || dwGameType, not the expansion flag.
    int nativeClass{}, level{}, minimumLife{}, maximumLife{}, defense{}, attackRating{};
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
    NativeCollisionPattern collisionPattern{NativeCollisionPattern::None};
    bool demon{}, undead{}, boss{}, lesserUndead{};
    int stunState{-1};
    bool convertible{};
    bool curseable{},switchAi{},revivable{};
    bool curseAiRunMode{},curseAiInteract{};
    int curseAiRunVelocity{};
    NativeMonsterStateTarget stateTarget;
    bool noAura{};
    int petType{},petVariant{-1};
    int64_t corpseBaseLife{};
    bool primeEvil{};
    bool environmentController{};
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
    Vec spawnOffset;
    uint8_t spawnMode{1};
    int spawnTicks{}, skillMoveState{-1};
    int baseClass{}, blueState{-1}, redState{-1};
    std::map<uint8_t,int> aiPoseTicks{};
    int summonBirthTicks{},summonLifeFrames{};
    std::optional<AuraDefinition> nativeAura{};
    std::shared_ptr<const std::map<int,AuraSkillSpec>> auraPrograms;
    int noManaRegenState=-1;
    std::optional<MonsterMissileRule> hitBurst{}, deathBurst{};
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
    std::map<int,MonsterMissileRule> enchantmentMissiles;
    struct AuraStats { std::vector<StatValue> targets,owner; };
    std::map<int,AuraStats> auraStats;
    bool introduction{};
    int preventHealState{-1};
    bool knockbackOnHit{};
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

#pragma once
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/monsters/ai_spec.hpp"
#include "gameplay/monsters/combat_values.hpp"
#include "gameplay/monsters/damage.hpp"
#include "gameplay/monsters/unique_modifiers.hpp"
#include "gameplay/skills/firewall_spec.hpp"
#include "gameplay/combat/damage_type.hpp"
#include "world/navigation.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace d2x::server {
struct PreparedMonster;
// Value-only admission contracts; archive/table lookups belong to hosting.
struct AttackAnimation { int frames{}, speed{}, actionFrame{}, startFrame{}; };
struct MeleeRules { std::map<std::string, AttackAnimation, std::less<>> animations; };
struct MonsterMissileRule {
    enum class Behavior { Projectile, Fireball, FireHead, SpiderLay, SpiderGoo, FirewallMaker, Fire, Charged, ColdNova } behavior{Behavior::Projectile};
    int definition{-1}, frames{}, sourceDamage{}, minimum{}, maximum{}, elementalMinimum{}, elementalMaximum{};
    int nextDelay{}, hitClass{};
    uint64_t coldFrames{}, poisonFrames{};
    DamageType element{DamageType::Physical};
    float speed{};
    MissileCollisionRule collision;
    bool killOnHit{}, clientSend{}, returnFire{}, toHit{};
    int baseVelocity{}, levelVelocity{}, rank{1}, activate{};
    float blastRadius{};
    bool canSlow{}, collidePlayers{true}, collideMonsters{true}, alwaysExplode{};
    bool noMultiShot{}, noUniqueMod{}, unspreadMultiShot{};
    float acceleration{}, maximumVelocity{};
};
// Native monster modes are distinct from Skills.txt identities. A1/A2 keep
// their own release frame, accuracy and damage even when no skill is assigned.
struct MonsterAttackRule {
    int minimum{}, maximum{}, rating{}, duration{}, release{};
    std::vector<MonsterElementAttack> elements;
    std::optional<MonsterMissileRule> missile{};
    enum class Action { Damage, Resurrect, Nest, Web, Spray, Firewall, Teleport, Trap } action{Action::Damage};
    uint8_t nativeMode{4}, rank{1};
    std::optional<MonsterMissileRule> extraQuill{};
    std::vector<int> releaseFrames{};
    std::optional<MonsterMissileRule> groundFire{};
};
struct MonsterWebRule {
    int missile{-1}, frames{}, auraFrames{}, slowFrames{}, slowPercent{};
    float radius{};
    CombatStateDefinition aura, slow;
    MonsterMissileRule lay{}, ground{};
};
struct MonsterRule {
    int nativeClass{}, level{}, minimumLife{}, maximumLife{}, defense{}, attackRating{};
    int minimumDamage{}, maximumDamage{}, criticalChance{};
    std::array<int, 6> resistances{};
    int size{}, meleeRange{}, attackTicks{}, impactTick{}, deathTicks{}, decisionTicks{};
    int nativeVelocity{}, difficulty{};
    MovementCollisionRule collision;
    MovementCollisionRule spawnCollision;
    bool demon{}, undead{};
    int coldEffect{}, coldState{-1}, frozenState{-1};
    int knockbackTicks{};
    std::vector<uint64_t> experience;
    MonsterAiProfile ai{};
    std::map<uint8_t, MonsterAttackRule> attacks;
    MonsterHitStates hitStates;
    int damageRegen{};
    std::map<uint16_t,MonsterAttackRule> skillActions;
    std::array<uint16_t,4> skillIds{};
    std::shared_ptr<const PreparedMonster> nestChild;
    Vec spawnOffset;
    std::optional<MonsterWebRule> web;
    bool corpseSelectable{}, opensDoors{};
    int resurrectionTicks{}, threat{};
    int hitRecoveryTicks{}, blockTicks{}, blockChance{}, coldDivisor{1};
    bool blockWithoutShield{};
    std::array<uint8_t,16> componentCounts{};
    uint8_t totalPieces{};
    uint8_t hitClass{};
    std::vector<bool> shieldChoices;
    std::optional<MonsterEnchantment> enchantment{};
    int superUniqueIndex{-1}, uniqueTranslation{-1};
    std::optional<MonsterFirewall> firewall;
    std::map<int,MonsterMissileRule> enchantmentMissiles;
    struct AuraStats { std::vector<std::pair<int,int64_t>> targets,owner; };
    std::map<int,AuraStats> auraStats;
    bool introduction{};
    int preventHealState{-1};
    bool knockbackOnHit{};
    struct DeathSweep {int radius{},minimumDelay{},maximumDelay{};bool undeadOnly{};};
    std::optional<DeathSweep> deathSweep;
};
struct PreparedMonster {
    MonsterIdentity identity{};
    MonsterKind implementation{};
    Vec position;
    MonsterRule rule;
    std::vector<Vec> skillPositions{};
};
}

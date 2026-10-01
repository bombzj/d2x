#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include <map>
#include <optional>

namespace d2x {
// Species, allegiance and ownership are independent. Faction IDs are open ended;
// an unconfigured relationship is neutral, never implicitly hostile.
enum class CombatRole { Player, Monster, Hireling, Summon };
enum class Relation { Neutral, Allied, Hostile };
struct CombatIdentity {
    unsigned faction = 0;
    EntityId owner;
    unsigned party = 0;
    CombatRole role = CombatRole::Monster;
    bool attackable = true;
};
struct CombatRelations {
    std::map<std::pair<unsigned, unsigned>, Relation> factions{
        {{1, 2}, Relation::Hostile}, {{2, 1}, Relation::Hostile}};
    // Directional overrides support hostility without changing a unit's species.
    std::map<std::pair<EntityId, EntityId>, Relation> units;
};
struct UnitCombatStats {
    CharacterAttributes attributes;
    int level = 1, block = 0, collisionSize = 2, drain = 100;
    int critical = 0, damageRegen = 0, followVelocityBonus = 0;
    float minimumDamage = 0, maximumDamage = 0;
    bool resolved = true;
    bool monsterResistanceRules = false;
    bool demon = false, undead = false, boss = false, primeEvil = false;
    bool freezable = true;
    MonsterRank rank = MonsterRank::Normal;
};
struct PlayerState;
struct HirelingState;
struct Enemy;
struct CombatEffectSet;
// Ephemeral adapter; never retained across entity insertion/removal or travel.
struct CombatUnit {
    EntityId id;
    CombatIdentity identity;
    Vec *position = nullptr;
    float *life = nullptr, *mana = nullptr, *chill = nullptr;
    float *poisonRate = nullptr, *poisonTime = nullptr;
    uint64_t *random = nullptr;
    CombatEffectSet *effects = nullptr;
    UnitCombatStats stats;
    PlayerState *player = nullptr;
    HirelingState *hireling = nullptr;
    Enemy *monster = nullptr;
    explicit operator bool() const { return life != nullptr; }
    bool alive() const { return life && *life > 0; }
};
enum class DamagePermission { Hostile, ExistingEffect, Environment, Debug };
struct DamageRequest {
    EntityId attacker, defender;
    float amount = 0;
    MonsterDamageType type = MonsterDamageType::Physical;
    float chill = 0;
    bool mitigated = false, hitRecovery = true, freeze = false;
    // Only an existing periodic effect, an environment hazard or an explicit
    // debug command may bypass allegiance. Ordinary attacks always check it.
    DamagePermission permission = DamagePermission::Hostile;
    std::array<float, 6> channels{}; // Optional simultaneous channels: one hit/death transition.
    int freezeFrames = 0; // Native unmitigated freeze length, resolved before the death transition.
    int hitClass = -1;
    bool softHit = false;
};
} // namespace d2x

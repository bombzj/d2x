#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include <deque>
#include <map>
#include <string>

namespace d2x {
struct Restoration {
    float remaining, rate;
};
struct SkillHotkey {
    int skill = -2; // -2 unbound, -1 ordinary attack, otherwise MPQ skill ID.
    bool right = true;
};
// Transient skill, monster, shrine and item states share a single modifier
// source. Runtime triggers may add entries; character saves omit them.
struct ActiveCombatEffect {
    CombatEffectSource source = CombatEffectSource::Skill;
    EntityId owner;
    int sourceId = -1;
    float expiresAt = 0;
    CharacterModifiers modifiers;
};
struct PlayerState {
    EntityId id;
    std::string characterClass = "Barbarian";
    Vec pos, previous, look{1, 0};
    std::deque<Vec> route;
    float hp = 0, mana = 0, stamina = 0;
    float castTime = 0, spinTime = 0, leapTime = 0, hitTime = 0, deathTime = 0, meleeTime = 0;
    float lastMeleeDuration = 0;
    float chill = 0;
    float poisonRemaining = 0, poisonPerSecond = 0;
    float webSlowRemaining = 0;
    int webSlowPercent = 0;
    EntityId webSource;
    Vec leapStart, leapEnd;
    std::array<float, skillCount> cooldown{};
    std::deque<Restoration> healing, manaRestoration;
    float staminaBoost = 0;
    EntityId attackTarget;
    bool throwAttack = false;
    bool leftHandAttack = false;
    Skill lastSkill = Skill::Fireball;
    float lastCastDuration = .32f;
    bool running = true, moving = false, dead = false;
    uint64_t combatRandom = (uint64_t(666) << 32) | 210;
    unsigned nextWeapon = 0;
    unsigned gold = 0, bankGold = 0;
    uint64_t experience = 0;
    int level = 1;
    AttributeAllocation allocated;
    int unspentAttributes = 0;
    std::map<int, int> skillRanks;
    std::vector<ActiveCombatEffect> combatEffects;
    int unspentSkills = 0;
    std::array<SkillHotkey, 8> skillHotkeys{};
};
struct Enemy {
    EntityId id;
    MonsterKind kind = MonsterKind::Fallen;
    MonsterIdentity identity;
    Vec pos;
    float hp = 100, maxHp = 100, chill = 0, attack = 0;
    float attackDuration = 0, attackImpact = -1;
    int attackMode = 1;
    float skill2Remaining = 0, skill2Duration = 0;
    float stun = 0, deathAge = 0, hitFlash = 0, rethink = 0;
    float aiWait = 0;
    bool aiPursuing = false;
    bool aiEscaping = false;
    bool aiCommanded = false;
    bool aiCircling = false;
    bool aiRunning = false;
    bool aiRetaliate = false;
    bool aiCharged = false;
    float aiAdvanceRemaining = 0;
    int aiPhase = 0, aiLoop = 0;
    EntityId aiCorpse;
    bool resurrected = false;
    float webAuraRemaining = 0, webTrailDistance = 0;
    std::deque<Vec> route;
    uint64_t combatRandom = (uint64_t(666) << 32) | 210;
    float poisonRemaining = 0, poisonPerSecond = 0;
    EntityId poisonSource;
};
struct Missile {
    EntityId id, owner;
    Vec pos, velocity;
    float remaining = 2;
    Skill skill = Skill::Fireball;
    bool physical = false;
    int missileId = -1;
    float damage = 0;
    float radius = 0, chill = 0;
    bool hostile = false;
    int hostileMode = 0;
    float slowDuration = 0;
    AttackElements attackElements{};
};
struct Effect {
    Vec pos;
    Skill skill;
    float age = 0, duration = .8f;
};
// Area combat state survives travel. Ground item ownership lives in session InventoryState.
struct AreaState {
    RegionId region = RegionId::Encampment;
    std::vector<Enemy> enemies;
    std::vector<MonsterSpawn> pendingSpawns;
    std::vector<Missile> missiles;
    std::vector<Effect> effects;
    int kills = 0;
    bool initialized = false;
};
// Player combat state survives travel. GameSession separately owns inventory/container state.
struct TownPortalState {
    bool active = false;
    uint64_t revision = 0;
    RegionId field = RegionId::Encampment;
    Vec fieldPosition, townPosition;
};
struct WorldState {
    uint32_t mapSeed = 210;
    PopulationSettings population;
    PlayerState player;
    AreaState area;
    float time = 0;
    std::string message;
    TownPortalState portal;
    std::map<RegionId, float> waypoints;
};
} // namespace d2x

#pragma once
#include "core/id.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include "gameplay/quest/state.hpp"
#include "gameplay/npc/hireling.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/combat/weapon_attack.hpp"
#include "gameplay/combat/weapon_projectile.hpp"
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace d2x {
struct Restoration {
    float remaining, rate;
};
struct SkillHotkey {
    int skill = -2; // -2 unbound, -1 ordinary attack, otherwise MPQ skill ID.
    bool right = true;
};
struct PlayerState {
    struct PendingCast {
        SkillCastSpec skill;
        Vec target;
        int staticFieldMinimum = 0;
        float remaining = 0;
        EntityId enemy;
    };
    struct ChannelCast {
        SkillCastSpec skill;
        Vec target;
        float remaining = 0;
        unsigned pulses = 0;
        float age = 0;
        EntityId enemy;
    };
    EntityId id;
    std::string name = "Hero";
    std::string characterClass = "Barbarian";
    std::string nativeSaveSections;
    Vec pos, previous, look{1, 0};
    std::deque<Vec> route;
    float hp = 0, mana = 0, stamina = 0;
    float castTime = 0, hitTime = 0, deathTime = 0, meleeTime = 0;
    std::optional<WeaponAttackState> weaponAttack;
    EffectFrame skillDelayUntil = 0;
    float chill = 0;
    float poisonRemaining = 0, poisonPerSecond = 0;
    float webSlowRemaining = 0;
    int webSlowPercent = 0;
    EntityId webSource;
    std::deque<Restoration> healing, manaRestoration;
    EntityId attackTarget;
    std::optional<Vec> attackPosition;
    bool attackStationary = false;
    bool throwAttack = false;
    bool leftHandAttack = false;
    float lastCastDuration = 0;
    float lastCastRate = 0;
    std::optional<PendingCast> pendingCast;
    std::optional<ChannelCast> channel;
    int channelSkill() const { return channel ? channel->skill.sourceId : -1; }
    float channelAge() const { return channel ? channel->age : 0; }
    bool running = false, runningNow = false, moving = false, dead = false;
    uint64_t combatRandom = 0; // Initialized on unit creation.
    unsigned weaponSet = 0;
    unsigned gold = 0, bankGold = 0;
    std::array<std::set<std::string>, 3> npcIntroductions;
    uint64_t experience = 0;
    int level = 1;
    AttributeAllocation allocated;
    int unspentAttributes = 0;
    std::map<int, int> skillRanks;
    CombatEffectSet combatEffects;
    int unspentSkills = 0;
    std::array<SkillHotkey, 8> skillHotkeys{};
    std::array<int, 4> selectedSkills{-1, -1, -1, -1};
    ActOneQuestBook actOneQuests{};
    HirelingState hireling;
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
    float resurrectionRemaining = 0, resurrectionDuration = 0;
    float stun = 0, freeze = 0, deathAge = 0, hitFlash = 0, rethink = 0;
    float aiWait = 0;
    bool aiPursuing = false;
    bool aiEscaping = false;
    bool aiCommanded = false;
    bool aiCircling = false;
    bool aiRunning = false;
    std::optional<int> movementVelocityPercent;
    bool aiRetaliate = false;
    bool aiCharged = false;
    float aiAdvanceRemaining = 0;
    int aiPhase = 0, aiLoop = 0;
    EntityId aiCorpse;
    bool resurrected = false;
    float webAuraRemaining = 0, webTrailDistance = 0;
    std::deque<Vec> route;
    uint64_t combatRandom = 0; // Initialized on unit creation.
    float poisonRemaining = 0, poisonPerSecond = 0;
    EntityId poisonSource;
    bool poisonPlayerEffects = true;
    float openWoundsRemaining = 0, openWoundsPerSecond = 0;
    EntityId openWoundsSource;
    bool openWoundsPlayerEffects = true;
    EffectFrame nextAuraFrame = 0, nextUniqueLightningFrame = 0, deathEnchantmentFrame = 0;
    EffectFrame pendingUniqueLightningFrame = 0;
    std::optional<Vec> teleportTarget = std::nullopt;
    CombatEffectSet combatEffects;
};
struct Missile {
    EntityId id, owner;
    Vec pos, velocity;
    float remaining = 2;
    SkillBehavior behavior = SkillBehavior::None;
    bool physical = false;
    int missileId = -1;
    float damage = 0;
    float radius = 0, chill = 0;
    bool hostile = false;
    int hostileMode = 0;
    float slowDuration = 0;
    AttackElements attackElements{};
    int attackerLevel = 0, attackRating = 0;
    float nextHitDelay = 0;
    float age = 0;
    float acceleration = 0, maxVelocity = 0;
    int hitOverlayId = -1;
    float hitOverlayDuration = 0;
    EntityId lastHit{};
    std::deque<Vec> path{};
    bool groundTargeted = false;
    std::optional<MissileImpactSpec> impact = {};
    MissileImpactDamage impactDamage = {};
    std::optional<PoisonCloudSpec> poisonCloud = {};
    uint64_t combatRandom = 0;
    int physicalDamagePercent = 0;
    int baseAttackRating = 0, attackRatingPercent = 0;
    AttackTargetModifiers targetModifiers = {};
    bool playerAttack = false;
    int skillId = -1, skillRank = 0;
    std::optional<MonsterDamageType> hostileElement = std::nullopt;
    bool killOnHit = true;
};
struct Effect {
    Vec pos;
    float age = 0, duration = .8f;
    int missileId = -1;
    int overlayId = -1;
    EntityId attached{};
};
// Area combat state survives travel. Ground item ownership lives in session InventoryState.
struct AreaState {
    RegionId region = RegionId::Encampment;
    std::vector<Enemy> enemies;
    std::vector<MonsterSpawn> pendingSpawns;
    std::vector<Missile> missiles;
    std::vector<Effect> effects;
    std::map<EntityId, float> novaHitUntil;
    int kills = 0;
    bool initialized = false;
};
// Player combat state survives travel. GameSession separately owns inventory/container state.
struct TownPortalState {
    bool active = false;
    uint64_t revision = 0;
    RegionId field = RegionId::Encampment;
    Vec fieldPosition, townPosition;
    float openedAt = 0;
    bool consumedOnReturn = true;
};
struct WorldState {
    uint32_t mapSeed = 0;
    PopulationSettings population;
    PlayerState player;
    AreaState area;
    EffectFrame frame = 0;
    float time = 0;
    std::string message;
    TownPortalState portal;
    std::vector<TownPortalState> publicPortals;
    uint64_t nextPortalRevision = 0;
    std::map<RegionId, float> waypoints;
};
} // namespace d2x

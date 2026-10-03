#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/combat/stat_modifiers.hpp"
#include "gameplay/combat/missile_effects.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/elemental_spec.hpp"
#include <deque>
#include <optional>

namespace d2x {
struct FrozenOrbMissileState {
    enum class Phase { Orb, Bolt, Nova };
    Phase phase = Phase::Orb;
    FrozenOrbCastSpec spec;
    int elapsedFrames = 0, emissionDirection = 0;
    Vec novaTarget;
    int minimumDamage = 0, maximumDamage = 0, coldFrames = 0;
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
    bool monsterAttack = false; // Payload format, never a faction/target filter.
    int monsterAttackMode = 0;
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
    bool weaponAttack = false;
    int skillId = -1, skillRank = 0;
    std::optional<MonsterDamageType> fixedElement = std::nullopt;
    bool killOnHit = true;
    std::optional<FrozenOrbMissileState> frozenOrb = std::nullopt;
    struct BlizzardState {
        BlizzardSpec spec;
        bool center = true;
        int elapsedFrames = 0, lifetimeFrames = 0, spawnSeedX = 0;
        int minimumDamage = 0, maximumDamage = 0, coldFrames = 0;
    };
    std::optional<BlizzardState> blizzard = std::nullopt;
    struct FreezingAreaState {
        int elapsedFrames = 0, lifetimeFrames = 0;
        int minimumDamage = 0, maximumDamage = 0;
    };
    std::optional<FreezingAreaState> freezingArea = std::nullopt;
    struct ColdRetaliationState {
        int elapsedFrames = 0, lifetimeFrames = 0;
        int minimumDamage = 0, maximumDamage = 0, coldFrames = 0;
    };
    std::optional<ColdRetaliationState> coldRetaliation = std::nullopt;
    struct FirewallState {
        FirewallSpec definition;
        bool maker = false;
        int elapsedFrames = 0;
    };
    std::optional<FirewallState> firewall = std::nullopt;
    struct ArcState {
        ArcSpec spec;
        int remainingHits = 1, minimumDamage = 0, maximumDamage = 0;
    };
    std::optional<ArcState> arc = std::nullopt;
    std::optional<MeteorSpec> meteor = std::nullopt;
    float healingMinimum = 0, healingMaximum = 0;
    std::optional<HeavenSpec> heaven = std::nullopt;
    EntityId heavenTarget{};
};
struct Effect {
    Vec pos;
    float age = 0, duration = .8f;
    int missileId = -1;
    int overlayId = -1;
    EntityId attached{};
};
} // namespace d2x

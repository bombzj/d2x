#pragma once
#include "gameplay/combat/identity.hpp"
#include "gameplay/combat/stats.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/unique_modifiers.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include <deque>
#include <optional>

namespace d2x {
struct MonsterApproach {
    // Target-unit movement uses D2Common_10399 and StepNum - 1. Coordinate
    // actions (Wraith) retain the destination chosen by the AI until arrival.
    std::optional<Vec> destination;
    int stopDistance = 0, velocityPercent = 75;
    bool running = false;
};
struct Enemy {
    EntityId id;
    MonsterKind kind = MonsterKind::Fallen;
    MonsterIdentity identity;
    std::optional<MonsterEnchantment> enchantment;
    const MonsterEnchantment *enchantmentData() const {
        return enchantment ? &*enchantment : nullptr;
    }
    Vec pos;
    float hp = 100, maxHp = 100, chill = 0, attack = 0;
    float attackDuration = 0, attackImpact = -1;
    int attackMode = 1;
    int attackRatePercent = 100;
    size_t attackEventIndex = 0;
    CombatIdentity allegiance{2, {}, 0, CombatRole::Monster};
    EntityId combatTarget;
    CurseAi activeCurseAi = CurseAi::None;
    struct TerrorMovement {
        EntityId threat;
        int velocityBonus = 0;
        bool running = false, beganEscape = false;
    };
    std::optional<TerrorMovement> terrorMovement;
    EntityId attractedTarget;
    EffectFrame attractedUntil = 0;
    EffectSource attractionSource;
    std::optional<UnitCombatStats> intrinsicCombat;
    struct ConversionState {
        CombatIdentity original;
        EffectFrame expiresAt = 0;
        int level = 0, convertedLevel = 0;
        float maximumLife = 0;
        int state = -1;
    };
    std::optional<ConversionState> conversion;
    bool corpseConsumed = false;
    bool deathHidden = false, deathShattered = false, deathUnselectable = false;
    bool corpseAvailable() const { return hp <= 0 && !corpseConsumed && !deathUnselectable; }
    int summonSkill = -1, summonRank = 0;
    int summonShield = 0;
    float skill2Remaining = 0, skill2Duration = 0;
    float resurrectionRemaining = 0, resurrectionDuration = 0;
    float stun = 0, freeze = 0, deathAge = 0, hitFlash = 0, rethink = 0;
    float hitDisplay = 0;
    float hitRecoveryDuration = 0;
    float knockbackRemaining = 0, knockbackDuration = 0;
    std::optional<Vec> knockbackDestination;
    Vec knockbackFacing;
    bool freezeActive = false; // Native freeze bit, including a zero-length post-divisor application.
    float aiWait = 0;
    float webSlowRemaining = 0;
    int webSlowPercent = 0;
    bool aiPursuing = false;
    bool aiEscaping = false;
    bool aiCommanded = false;
    bool aiCircling = false;
    bool aiRunning = false;
    std::optional<MonsterApproach> approach;
    std::optional<int> movementVelocityPercent;
    bool aiRetaliate = false;
    bool aiAlerted = false;
    bool aiCharged = false;
    float aiAdvanceRemaining = 0;
    int aiPhase = 0, aiLoop = 0;
    Vec aiHome;
    std::vector<Vec> skillPositions;
    EffectFrame skillCycleFrame = 0, questDeathFrame = 0;
    std::optional<Vec> skillPosition;
    EffectFrame nestLastCastFrame = 0;
    std::optional<Vec> nestSpawnPosition;
    bool noTreasure = false;
    EntityId aiCorpse;
    bool resurrected = false;
    struct HydraState {
        SkillCastSpec skill;
        RegionId region = RegionId::Encampment;
        EffectFrame expiresAt = 0;
        bool active = true;
    };
    std::optional<HydraState> hydra;
    bool living() const { return hydra ? hydra->active : hp > 0; }
    float webAuraRemaining = 0, webTrailDistance = 0;
    std::deque<Vec> route;
    uint64_t combatRandom = 0; // Initialized on unit creation.
    float poisonRemaining = 0, poisonPerSecond = 0;
    EntityId poisonSource;
    float openWoundsRemaining = 0, openWoundsPerSecond = 0;
    EntityId openWoundsSource;
    EffectFrame nextAuraFrame = 0, nextUniqueLightningFrame = 0, deathEnchantmentFrame = 0;
    EffectFrame pendingUniqueLightningFrame = 0;
    std::optional<Vec> teleportTarget = std::nullopt;
    CombatEffectSet combatEffects;
};
} // namespace d2x

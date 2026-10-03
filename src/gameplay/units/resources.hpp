#pragma once
#include <span>

namespace d2x {
struct RemovedCombatEffect;
struct ResourceRecovery {
    int maximumLife = 1, maximumMana = 1;
    float manaPerSecond = 0;
    int replenishLife = 0;
    bool suppressMana = false;
};
struct StaminaRecovery {
    int maximum = 1;
    float drain = 0;
    int recoveryBonus = 0;
};
struct StaminaActivity {
    bool moving = false, running = false, idle = false, safeZone = false;
};
// These policies preserve the existing multiplication order after 8.8 truncation.
enum class LifeRegenOrder { StepThenFrameRate, FrameRateThenStep };

// Callers provide current derived values and choose the original eligibility
// and fixed-step phase. These functions neither query nor own actor state.
void advanceResourceRecovery(float &life, float &mana, const ResourceRecovery &recovery, float dt);
void advanceLifeRegeneration(float &life, float maximum, int damageRegen, float dt, LifeRegenOrder order);
void advanceStamina(float &stamina, const StaminaRecovery &recovery, const StaminaActivity &activity, float dt);
void restoreStaminaOnEffectRemoval(float &stamina, float maximum, std::span<const RemovedCombatEffect> removed);
} // namespace d2x

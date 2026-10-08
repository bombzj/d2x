#include "gameplay/units/resources.hpp"
#include "gameplay/effects/state.hpp"
#include <algorithm>
#include <cstdint>

namespace d2x {
void advanceResourceRecovery(float &life, float &mana, const ResourceRecovery &recovery, float dt) {
    if (!recovery.suppressMana)
        mana = std::min(float(recovery.maximumMana), mana + dt * recovery.manaPerSecond);
    if (recovery.replenishLife)
        life = std::clamp(life + dt * recovery.replenishLife * 25.f / 256.f,
                          1.f, float(recovery.maximumLife));
}
void advanceLifeRegeneration(float &life, float maximum, int damageRegen, float dt, LifeRegenOrder order) {
    const int perFrame = int(maximum * 256.f * damageRegen / 4096.f);
    if (order == LifeRegenOrder::StepThenFrameRate)
        life = std::min(maximum, life + perFrame / 256.f * dt * 25.f);
    else
        life = std::min(maximum, life + float(perFrame) / 256.f * 25.f * dt);
}
void advanceStamina(float &stamina, const StaminaRecovery &recovery, const StaminaActivity &activity, float dt) {
    float rate = 0;
    if (activity.moving && activity.running && !activity.safeZone)
        rate = -recovery.drain;
    const bool walking = activity.moving && !activity.running;
    if (activity.idle || walking || recovery.recoveryBonus >= 1000) {
        // PlrModes: running drain and EVENTS_StaminaRegen are independent.
        // A large recovery stat permits regeneration in non-walk/idle modes;
        // it does not switch off drain. Preserve the native 8.8 rounding.
        if (!walking || stamina >= 1.f || activity.safeZone) {
            int64_t amount = (int64_t(recovery.maximum) * 256) >> (walking ? 9 : 8);
            amount += amount * recovery.recoveryBonus / 100;
            rate += float(std::max<int64_t>(0, amount)) * 25.f / 256.f;
        }
    }
    stamina = std::clamp(stamina + dt * rate, 0.f, float(recovery.maximum));
}
void restoreStaminaOnEffectRemoval(float &stamina, float maximum, std::span<const RemovedCombatEffect> removed) {
    for (const auto &entry : removed)
        if (entry.effect.spec.restoreStaminaOnRemoval)
            stamina = maximum;
}
} // namespace d2x

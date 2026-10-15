#include "gameplay/units/resources.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace d2x {
std::optional<ConversionResourceChange> conversionResources(Fixed8 life,Fixed8 maximum,int32_t targetLevel,int32_t casterLevel) {
    if(!targetLevel || casterLevel>=targetLevel) return {};
    const int32_t maximumWhole=maximum.raw()>>8;
    const auto reducedMaximum=std::max(1,nativeShiftLeft(nativePercentage(maximumWhole,casterLevel,targetLevel),8));
    const auto reducedLife=std::min(reducedMaximum,std::max(1,nativeShiftLeft(nativePercentage(life.raw()>>8,casterLevel,targetLevel),8)));
    return ConversionResourceChange{{targetLevel,maximumWhole},Fixed8::fromRaw(reducedLife),Fixed8::fromRaw(reducedMaximum)};
}
Fixed8 conversionRestoredLife(const ConversionResourceSave &saved,Fixed8 life,Fixed8 maximum) {
    const int32_t maximumWhole=maximum.raw()>>8;
    const auto restored=maximumWhole?nativePercentage(saved.maximumWhole,life.raw()>>8,maximumWhole):1;
    return Fixed8::fromRaw(nativeShiftLeft(restored,8));
}
void advanceResourceRecovery(Fixed8 &life, Fixed8 &mana, const ResourceRecovery &recovery) {
    if (!recovery.suppressMana)
        mana = Fixed8::fromRaw(std::clamp(nativeAdd(mana.raw(), recovery.manaPerFrame), 0, recovery.maximumMana.raw()));
    if (recovery.replenishLife)
        life = Fixed8::fromRaw(std::clamp(nativeAdd(life.raw(), recovery.replenishLife), 256, recovery.maximumLife.raw()));
}
void advanceLifeRegeneration(Fixed8 &life, Fixed8 maximum, int32_t hpRegen) {
    const auto next=std::min(maximum.raw(),nativeAdd(life.raw(),hpRegen));
    life=Fixed8::fromRaw(next<1?0:next);
}
Fixed8 resourceOnMaximumChange(Fixed8 current,Fixed8 previousMaximum,Fixed8 maximum) {
    // Retail 1.13c D2Game RVA D95CC/D95DD/D95DF: divide, multiply,
    // truncate to int64, then compare the signed low 32 bits before clamping.
    if(previousMaximum==maximum || previousMaximum<=0 || current<=0) return current;
    const auto previous=std::max(previousMaximum.raw(),256);
    const auto scaled=int64_t(double(current.raw())/double(previous)*double(maximum.raw()));
    // Retail clamps low first, then high. A zero/negative temporary maximum
    // therefore wins the high clamp; std::clamp(1,maximum) is invalid here.
    return Fixed8::fromRaw(std::min(std::max(nativeNarrow(scaled),1),maximum.raw()));
}
int32_t monsterRegenerationOnMaximumChange(Fixed8 maximum,int damageRegen) {
    // This callback truncates MAXHP before multiplying, unlike admission's
    // unsigned raw-MAXHP product. Preserve both native operations separately.
    return nativeMultiply(damageRegen,maximum.raw()>>8)>>4;
}
void applyResourceMaximums(Fixed8 &life,Fixed8 &mana,Fixed8 &stamina,ResourceMaximums before,ResourceMaximums after) {
    life=resourceOnMaximumChange(life,before.life,after.life);
    mana=resourceOnMaximumChange(mana,before.mana,after.mana);
    stamina=resourceOnMaximumChange(stamina,before.stamina,after.stamina);
}
void advanceStamina(Fixed8 &stamina, const StaminaRecovery &recovery, const StaminaActivity &activity) {
    // Running mode and STATREGEN are separate native callbacks. No seconds or
    // floating-point projection participates in their resource calculation.
    if (activity.moving && activity.running && !activity.safeZone)
        stamina = Fixed8::fromRaw(std::max(0, nativeSubtract(stamina.raw(), recovery.drainPerFrame)));
    const bool walking = activity.moving && !activity.running;
    if (activity.idle || walking || recovery.recoveryBonus >= 1000) {
        // PlrModes: running drain and EVENTS_StaminaRegen are independent.
        // A large recovery stat permits regeneration in non-walk/idle modes;
        // it does not switch off drain. Preserve the native 8.8 rounding.
        const auto maximum = recovery.maximum.raw();
        if ((!walking || stamina.raw() >= 256 || activity.safeZone) && stamina.raw() < maximum) {
            auto amount = maximum >> (walking ? 9 : 8);
            amount = nativeAdd(amount, nativeMultiplyDivide(recovery.recoveryBonus, amount, 100));
            stamina = Fixed8::fromRaw(std::min(maximum, nativeAdd(stamina.raw(), amount)));
        }
    }
}
void restoreStaminaOnEffectRemoval(Fixed8 &stamina, Fixed8 maximum, std::span<const RemovedCombatEffect> removed) {
    for (const auto &entry : removed)
        if (entry.effect.spec.restoreStaminaOnRemoval && !entry.effect.spec.nativeState)
            stamina = maximum;
}
} // namespace d2x

#pragma once
#include "core/fixed.hpp"
#include <span>
#include <optional>

namespace d2x {
struct ResourceValues {Fixed8 life,mana,stamina;};
struct ResourceMaximums {
    Fixed8 life,mana,stamina;
    auto operator<=>(const ResourceMaximums &) const = default;
};
struct ConversionResourceSave {int32_t level{},maximumWhole{};};
struct ConversionResourceChange {ConversionResourceSave saved;Fixed8 life,maximum;};
// Paladin Conversion's saved maximum remains whole points. The caller must
// preserve its distinct retail MAXHP write and subsequent StatList callback.
std::optional<ConversionResourceChange> conversionResources(Fixed8 life,Fixed8 maximum,int32_t targetLevel,int32_t casterLevel);
Fixed8 conversionRestoredLife(const ConversionResourceSave &,Fixed8 life,Fixed8 maximum);
Fixed8 mindBlastRestoredLife(const ConversionResourceSave &,Fixed8 life,Fixed8 maximum);
struct RemovedCombatEffect;
struct ResourceRecovery {
    Fixed8 maximumLife=1,maximumMana=1;
    int manaPerFrame = 0;
    int replenishLife = 0;
    bool suppressMana = false;
};
struct StaminaRecovery {
    Fixed8 maximum=1;
    int drainPerFrame = 0;
    int recoveryBonus = 0;
};
struct StaminaActivity {
    bool moving = false, running = false, idle = false, safeZone = false;
};
// Callers provide current derived values and choose the original eligibility
// and fixed-step phase. These functions neither query nor own actor state.
void advanceResourceRecovery(Fixed8 &life, Fixed8 &mana, const ResourceRecovery &recovery);
// MonsterMode STATREGEN receives the effective raw HPREGEN stat. Admission
// and maximum-change callbacks prepare that rate; this consumer never rebuilds it.
void advanceLifeRegeneration(Fixed8 &life, Fixed8 maximum, int32_t hpRegen);
Fixed8 resourceOnMaximumChange(Fixed8 current,Fixed8 previousMaximum,Fixed8 maximum);
void applyResourceMaximums(Fixed8 &life,Fixed8 &mana,Fixed8 &stamina,ResourceMaximums before,ResourceMaximums after);
int32_t monsterRegenerationOnMaximumChange(Fixed8 maximum,int damageRegen);
void advanceStamina(Fixed8 &stamina, const StaminaRecovery &recovery, const StaminaActivity &activity);
void restoreStaminaOnEffectRemoval(Fixed8 &stamina, Fixed8 maximum, std::span<const RemovedCombatEffect> removed);
} // namespace d2x

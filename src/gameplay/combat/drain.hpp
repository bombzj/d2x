#pragma once
#include <cstdint>
namespace d2x {
struct NativeDrainInput {
    int32_t life{},mana{},stamina{},physical{},targetLife{},targetMana{},targetStamina{},drainPercent{},missingLife{};
    bool playerStyle{},playerDifficulty{};
    int32_t lifeDivisor{1},manaDivisor{1};
};
struct NativeDrainResult {
    int32_t lifeField{},manaField{},staminaField{},lifeGain{},manaGain{};
    bool lifeOverlay{},manaOverlay{},chooseOverlay{};
};
// SUNITDMG_ExecuteEvents interprets shared drain fields by actual unit type.
NativeDrainResult calculateNativeDrain(const NativeDrainInput &);
int32_t nativeLeechFromScaledPercent(int32_t physical,int32_t percentWithSixBits,int32_t drainPercent);
}

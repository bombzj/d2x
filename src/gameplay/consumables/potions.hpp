#pragma once
namespace d2x {
enum class PotionKind { Healing, Mana, Rejuvenation, Stamina };
struct PotionDefinition {
    PotionKind kind;
    float amount, seconds;
};
} // namespace d2x

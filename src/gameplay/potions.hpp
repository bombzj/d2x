#pragma once
#include <optional>
#include <string_view>

namespace d2x {
enum class PotionKind { Healing, Mana, Rejuvenation, Stamina };
struct PotionDefinition {
    PotionKind kind;
    float amount, seconds;
};
// Barbarian amounts from Blizzard's classic Arreat Summit. The 1.04 demo
// misc.txt contains eligibility/graphics, but no restoration amounts or timing.
inline std::optional<PotionDefinition> potionDefinition(std::string_view code) {
    if (code.size() == 3 && code[2] >= '1' && code[2] <= '5') {
        int level = code[2] - '1';
        constexpr float healing[] = {60, 120, 200, 360, 640};
        constexpr float mana[] = {20, 40, 80, 150, 250};
        if (code.starts_with("hp"))
            return PotionDefinition{PotionKind::Healing, healing[level], 8};
        if (code.starts_with("mp"))
            return PotionDefinition{PotionKind::Mana, mana[level], 5.12f};
    }
    if (code == "rvs")
        return PotionDefinition{PotionKind::Rejuvenation, .35f, 0};
    if (code == "rvl")
        return PotionDefinition{PotionKind::Rejuvenation, 1, 0};
    if (code == "vps")
        return PotionDefinition{PotionKind::Stamina, 1, 30};
    return std::nullopt;
}
} // namespace d2x

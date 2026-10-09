#include "potions.hpp"
#include "core/random.hpp"
#include <cmath>
#include <limits>
namespace d2x {
float potionRestorationAmount(const PotionDefinition &potion, std::string_view playerClass) {
    // D2Common ITEMS_GetBonusLifeBasedOnClass / ITEMS_GetBonusManaBasedOnClass.
    // pSpell03 shifts recovery values to 8.8 before the x1.5 operation.
    if (potion.kind == PotionKind::Healing) {
        if (playerClass.empty() || playerClass == "bar") return potion.amount * 2.f;
        if (playerClass == "ama" || playerClass == "pal" || playerClass == "ass") return potion.amount * 1.5f;
    } else if (potion.kind == PotionKind::Mana) {
        if (playerClass == "sor" || playerClass == "nec" || playerClass == "dru") return potion.amount * 2.f;
        if (playerClass == "ama" || playerClass == "pal" || playerClass == "ass") return potion.amount * 1.5f;
    }
    return potion.amount;
}
int64_t rollPotionRestoration(const PotionDefinition &potion, std::string_view playerClass, int attribute, uint64_t &random) {
    const double amount = double(potionRestorationAmount(potion, playerClass)) * 256;
    if (!std::isfinite(amount) || amount <= 0 || amount > double(INT32_MAX) / 2) return 0;
    int64_t value = int64_t(amount);
    if (attribute > 0) {
        const auto chance = rollRandom(random) % 100;
        if (chance < limitedRandom(random, uint32_t(attribute)) / 2) value *= 2;
    }
    return value;
}
}

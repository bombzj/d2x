#include "potions.hpp"
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
}

#pragma once
#include "gameplay/effects/definition.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "core/id.hpp"
#include <array>
#include <string_view>
#include <functional>
#include <optional>
namespace d2x {
class CombatEffectSet;
struct EffectApplication;
enum class PotionKind { Healing, Mana, Rejuvenation, Stamina, Remedy };
struct PotionDefinition {
    PotionKind kind;
    int amount{}; // Original calc: whole resource points, or rejuvenation percent.
    CombatStateDefinition state;
    CharacterModifiers modifiers;
    std::array<int, 2> cureStates{-1, -1};
    EffectFrame durationFrames = 0;
    bool curesPoison = false, curesCold = false;
    std::vector<StatValue> nativeWrites; // Misc stat/calc order, in original stat units.
};
int32_t potionRestorationFixed(const PotionDefinition &, std::string_view playerClass);
int64_t rollPotionRestoration(const PotionDefinition &, std::string_view playerClass, int attribute, uint64_t &random);
// pSpell03/09 post one native list on the actual drinker. Timed recovery
// combines the remaining signed frame count and the list's own raw rate.
EffectApplication applyPotionState(CombatEffectSet &,const PotionDefinition &,EntityId,EffectFrame,
    const StatRuleCatalog &,const CombatStateRules &,std::optional<int32_t> restoration={},
    const std::function<void()> &observe={},const std::function<void(StatValue)> &afterWrite={});
} // namespace d2x

#pragma once
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/modifiers.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace d2x {
struct ClassicData;
// Only complete, observed, active item instances. Slot indices are normalized by
// the wire adapter; the original NOEQUIP/BROKEN flags decide admission there.
struct CombatDisplayItem { EquipmentSlot slot; ItemInstance item; };
struct KickPanelStats {
    AssassinKickInput physical;
    CombatModifiers elements;
    int baseAttackRating{},attackRatingPercent{},progressiveRating{};
    uint8_t damageColor{},ratingColor{};
    struct Charge {int skill,rank,count,damageKind;};
    std::vector<Charge> charges;
};
struct CombatDisplayEquipment {
    CharacterModifiers modifiers;
    EquipmentStats equipment;
    EquipmentActor actor;
    int concentrationDamagePercent = 0;
    std::map<int,int> skillRanks;
    std::optional<KickPanelStats> kick;
};
CombatDisplayEquipment projectCombatEquipment(const ClassicData &, EquipmentActor,
    std::span<const CombatDisplayItem>, std::span<const ItemInstance> charms,
    std::span<const ResolvedItemStat> states, const std::map<int,int> &effectiveRanks, const std::map<int,int> &baseRanks,
    const std::set<int> &activeStates, const std::set<int> &accountedPassives={}, int baseDefense=0, int baseAttackRating=0,
    int baseMinimum=0, int baseMaximum=0, int shieldDefensePercent=0);
} // namespace d2x

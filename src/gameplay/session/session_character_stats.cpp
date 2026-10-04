#include "gameplay/session/session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/items/equipment_inventory.hpp"
#include "content/items/equipment_modifiers.hpp"
#include "content/skills/passive_data.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>

namespace d2x {
namespace {
CharacterModifiers activeModifiers(const CombatEffectSet &effects, EffectFrame now) {
    return effects.modifiers(now);
}
void applyPassiveRating(CharacterModifiers &modifiers, const CharacterState &character,
                        const CombatEffectSet &effects, const SkillCatalog &skills, EffectFrame frame) {
    applySkillPassives(modifiers, skills, character.skillRanks, effects, frame);
}
}
const ItemInstance *GameSessionImpl::usableEquipment(EquipmentSlot slot) const {
    return borrowEquipmentLoadout(inventory_, playerContainers_).usable(slot, equipmentActor());
}
EquipmentActor GameSessionImpl::equipmentActor() const {
    const auto &player = state().player;
    auto effects = activeModifiers(player.combatEffects, state().frame);
    auto base = deriveCharacterAttributes(characterDefinition_, player.character.level, player.character.allocated, effects);
    EquipmentActor baseActor{characterDefinition_.code, base.strength, base.dexterity, player.character.level,
                             base.blockFactor, player.character.weaponSet};
    auto modifiers = resolveEquipmentModifiers(content_, inventory_, playerContainers_, baseActor);
    mergeCharacterModifiers(modifiers, effects);
    auto stats = deriveCharacterAttributes(characterDefinition_, player.character.level, player.character.allocated, modifiers);
    return {characterDefinition_.code, stats.strength, stats.dexterity, player.character.level,
            stats.blockFactor, player.character.weaponSet};
}
void GameSessionImpl::refreshCharacter(bool fillGains) {
    auto &player = simulation_->state_.player;
    const auto previous = simulation_->state_.player.attributes;
    auto effects = activeModifiers(player.combatEffects, state().frame);
    auto base = deriveCharacterAttributes(characterDefinition_, player.character.level, player.character.allocated, effects);
    EquipmentActor baseActor{characterDefinition_.code, base.strength, base.dexterity, player.character.level,
                             base.blockFactor, player.character.weaponSet};
    auto modifiers = resolveEquipmentModifiers(content_, inventory_, playerContainers_, baseActor);
    mergeCharacterModifiers(modifiers, effects);
    applyPassiveRating(modifiers, player.character, player.combatEffects, content_.skills, state().frame);
    modifiers.baseLife += questBaseLife(player.character.quests);
    const int resistance = questResistance(player.character.quests);
    modifiers.fireResist += resistance; modifiers.coldResist += resistance;
    modifiers.lightningResist += resistance; modifiers.poisonResist += resistance;
    auto current = deriveCharacterAttributes(characterDefinition_, player.character.level, player.character.allocated,
                                             modifiers, simulation_->resistancePenalty_);
    EquipmentActor actor{characterDefinition_.code, current.strength, current.dexterity, player.character.level,
                         current.blockFactor, player.character.weaponSet};
    applyWarmth(current, player.character, characterDefinition_, inventory_, playerContainers_, actor);
    if (fillGains) {
        if (player.resources.hp > 0) player.resources.hp += current.maxLife - previous.maxLife;
        player.resources.mana += current.maxMana - previous.maxMana;
        player.resources.stamina += current.maxStamina - previous.maxStamina;
    }
    player.resources.hp = std::clamp(player.resources.hp, 0.f, float(current.maxLife));
    player.resources.mana = std::clamp(player.resources.mana, 0.f, float(current.maxMana));
    player.resources.stamina = std::clamp(player.resources.stamina, 0.f, float(current.maxStamina));
    simulation_->state_.player.attributes = current;
    simulation_->state_.player.equipment = deriveEquipmentStats(borrowEquipmentLoadout(inventory_, playerContainers_), actor,
                                                       modifiers.defense, modifiers.combat, current.baseAttackRating);
}
} // namespace d2x

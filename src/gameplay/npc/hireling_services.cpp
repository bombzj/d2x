#include "gameplay/session/session.hpp"
#include "content/equipment_modifiers.hpp"
#include "content/monster_experience.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
const HirelingDefinition *GameSession::hirelingDefinition() const {
    const auto row = state().player.hireling.sourceRow;
    for (const auto &entry : content_.hirelings) if (entry.sourceRow == row) return &entry;
    return nullptr;
}
const std::vector<HirelingOffer> *GameSession::hirelingOffers(EntityId npc) const {
    auto found = hirelingOffers_.find(npc);
    return found == hirelingOffers_.end() ? nullptr : &found->second;
}
bool GameSession::canHireFrom(EntityId npc) const {
    const auto *target = object(npc);
    const auto *seller = target ? monsterContent_.find(target->npcClass) : nullptr;
    if (!seller || !content_.stashLayout.expansion) return false;
    // SUnitNpc hire eligibility; the current world implements Act I only.
    if (target->npcClass == "kashya" && state().player.level < 8 &&
        quest(ActOneQuest::SistersBurialGrounds).stage != uint32_t(BurialStage::Rewarded)) return false;
    return std::any_of(content_.hirelings.begin(), content_.hirelings.end(), [&](const auto &entry) {
        return entry.seller == seller->index && entry.act == 1 &&
               entry.difficulty == state().population.difficulty + 1;
    });
}
bool GameSession::ensureHirelingOffers(EntityId npc) {
    if (auto found = hirelingOffers(npc); found && !found->empty()) return true;
    const auto *target = object(npc);
    const auto *seller = target ? monsterContent_.find(target->npcClass) : nullptr;
    if (!seller) return false;
    auto random = inventory_.state_.creationRandom;
    auto offers = planHirelingOffers(content_.hirelings, seller->index,
        state().population.difficulty, state().player.level, random);
    if (offers.empty()) return false;
    hirelingOffers_[npc] = std::move(offers);
    inventory_.state_.creationRandom = random;
    return true;
}
void GameSession::openHirelingList(EntityId npc) {
    if (engagedNpc_ != npc || state().player.dead || !region().definition.safe ||
        !canHireFrom(npc) || !ensureHirelingOffers(npc)) {
        simulation_.emit(InteractionFailed{npc, "No mercenaries are available."});
        return;
    }
    simulation_.emit(HirelingListOpened{npc});
}
void GameSession::assignHireling(const HirelingOffer &offer) {
    const auto found = std::find_if(content_.hirelings.begin(), content_.hirelings.end(),
        [&](const auto &d) { return d.sourceRow == offer.sourceRow; });
    if (found == content_.hirelings.end()) return;
    auto &player = simulation_.state_.player;
    HirelingState next;
    next.sourceRow = offer.sourceRow; next.classId = found->classId;
    next.nameKey = offer.nameKey; next.level = offer.level;
    next.hp = float(offer.stats.life); next.experience = offer.stats.experience;
    next.pos = player.pos;
    player.hireling = std::move(next);
}
void GameSession::grantDebugHireling() {
    // Idempotent developer command: do not replace an existing hireling or their items.
    if (state().player.hireling.sourceRow >= 0 || state().player.dead) return;
    for (const auto &definition : content_.hirelings) {
        if (definition.act != 1 || definition.difficulty != state().population.difficulty + 1) continue;
        auto random = inventory_.state_.creationRandom;
        auto offers = planHirelingOffers(content_.hirelings, definition.seller,
            state().population.difficulty, state().player.level, random);
        if (offers.empty()) return;
        assignHireling(offers.front());
        inventory_.state_.creationRandom = random;
        return;
    }
}
void GameSession::grantHirelingExperience(const EnemyDied &death) {
    auto &hireling = simulation_.state_.player.hireling;
    const auto *definition = hirelingDefinition();
    if (!definition || !hireling.active() || hireling.level >= state().player.level || hireling.level >= 99) return;
    const auto award = resolveMonsterExperience(content_, monsterContent_, worldContent_,
        {death.identity, death.region, death.difficulty, hireling.level});
    if (!award.deferred.empty()) return;
    const auto stats = deriveHirelingStats(*definition, hireling.level);
    // SUnitDmg: cap one award to 1/64 of the level interval; owner's kills give 86/256.
    uint64_t amount = std::min(award.amount, (stats.nextExperience - stats.experience) >> 6);
    if (!death.hirelingKill) amount = amount * 86 / 256;
    hireling.experience += amount;
    if (hireling.experience < stats.nextExperience) return;
    ++hireling.level;
    for (const auto &entry : content_.hirelings)
        if (entry.id == definition->id && entry.difficulty == definition->difficulty &&
            entry.level <= hireling.level && entry.level > definition->level) definition = &entry;
    hireling.sourceRow = definition->sourceRow;
    hireling.hp = float(hirelingStats().base.life);
}
void GameSession::hireMercenary(const HireMercenary &command) {
    const auto *offers = hirelingOffers(command.npc);
    if (!offers || engagedNpc_ != command.npc || !region().definition.safe ||
        state().player.dead || !canHireFrom(command.npc)) return;
    auto selected = std::find_if(offers->begin(), offers->end(),
        [&](const auto &offer) { return offer.slot == command.slot; });
    if (selected == offers->end()) return;
    const auto offer = *selected;
    auto &player = simulation_.state_.player;
    if (uint64_t(player.gold) + player.bankGold < offer.stats.price) {
        simulation_.emit(InteractionFailed{command.npc, "Not enough gold."});
        return;
    }
    // Original hiring replaces the previous mercenary, including their equipment.
    for (auto id : inventory_.contents(playerContainers_.hirelingEquipment)) {
        const auto &item = *inventory_.item(id);
        simulation_.emit(ItemChange{id, item.revision, ItemChangeKind::Removed,
                                   item.location, {}, item.quantity});
        inventory_.state_.items.erase(id);
    }
    const unsigned wallet = std::min(player.gold, offer.stats.price);
    player.gold -= wallet; player.bankGold -= offer.stats.price - wallet;
    assignHireling(offer);
    auto &stock = hirelingOffers_.at(command.npc);
    std::erase_if(stock, [&](const auto &entry) { return entry.slot == command.slot; });
    engagedNpc_ = {};
    simulation_.emit(HirelingHired{command.npc});
}
HirelingCombatStats GameSession::hirelingStats() const {
    return hirelingStats(state().player.hireling, inventory_, playerContainers_);
}
HirelingCombatStats GameSession::hirelingStats(const HirelingState &hireling,
    const InventoryService &inventory, const PlayerContainers &containers) const {
    HirelingCombatStats result;
    const HirelingDefinition *definition = nullptr;
    for (const auto &entry : content_.hirelings)
        if (entry.sourceRow == hireling.sourceRow) { definition = &entry; break; }
    if (!definition) return result;
    result.base = deriveHirelingStats(*definition, hireling.level);
    PlayerContainers slots;
    slots.equipment = containers.hirelingEquipment;
    EquipmentActor actor{"", result.base.strength, result.base.dexterity, hireling.level};
    auto modifiers = resolveEquipmentModifiers(content_, inventory, slots, actor);
    actor.strength += modifiers.strength; actor.dexterity += modifiers.dexterity;
    result.base.strength = actor.strength; result.base.dexterity = actor.dexterity;
    result.base.life = std::max(1, (result.base.life + modifiers.maxLife) *
                                  (100 + modifiers.combat.lifePercent) / 100);
    auto combat = modifiers.combat;
    const bool armed = bool(inventory.equipped(slots, EquipmentSlot::RightHand));
    combat.minimumDamage += result.base.damageMin - (armed ? 0 : 1);
    combat.maximumDamage += result.base.damageMax - (armed ? 0 : 2);
    auto equipment = deriveEquipmentStats(inventory, slots, actor,
        result.base.defense + modifiers.defense, combat, result.base.attackRating + modifiers.attackRating);
    result.weapon = equipment.weapons[0];
    result.base.defense = equipment.defense;
    result.base.damageMin = result.weapon.minimum / 256;
    result.base.damageMax = result.weapon.maximum / 256;
    result.base.attackRating = result.weapon.attackRating;
    // Mercenaries use Hireling.txt's resistance, without player difficulty penalties.
    auto resist = [&](int bonus, int maximum) {
        return std::clamp(result.base.resist + bonus, -100, std::clamp(75 + maximum, 0, 95));
    };
    result.fireResist = resist(modifiers.fireResist, combat.fireMaxResist);
    result.coldResist = resist(modifiers.coldResist, combat.coldMaxResist);
    result.lightningResist = resist(modifiers.lightningResist, combat.lightningMaxResist);
    result.poisonResist = resist(modifiers.poisonResist, combat.poisonMaxResist);
    result.combat = modifiers.combat;
    return result;
}
InventoryError GameSession::previewHirelingEquipment(const EquipHirelingItem &command) const {
    const auto *d = hirelingDefinition();
    if (!d || !state().player.hireling.active() || state().player.dead)
        return InventoryError::AccessDenied;
    const auto *item = inventory_.item(command.item.id);
    if (!item) return InventoryError::UnknownItem;
    if (command.destination && !inventoryDestinationAllowed(*command.destination))
        return InventoryError::InvalidLocation;
    const auto &equipment = inventory_.catalog().find(item->definition)->equipment;
    if (command.slot && (*command.slot != EquipmentSlot::Head && *command.slot != EquipmentSlot::Torso &&
        *command.slot != EquipmentSlot::RightHand)) return InventoryError::RestrictedItem;
    if (command.slot == EquipmentSlot::RightHand &&
        !equipment.isType(d->weaponType1) && (d->weaponType2.empty() || !equipment.isType(d->weaponType2)))
        return InventoryError::RestrictedItem;
    auto slots = playerContainers_;
    slots.equipment = playerContainers_.hirelingEquipment;
    const auto stats = hirelingStats();
    EquipmentActor actor{"", stats.base.strength, stats.base.dexterity, state().player.hireling.level};
    return inventory_.preview(EquipItem{command.item, command.slot, command.destination},
                              slots, inventoryAccess(), actor);
}
void GameSession::equipHirelingItem(const EquipHirelingItem &command) {
    if (auto error = previewHirelingEquipment(command); error != InventoryError::None) {
        simulation_.emit(InventoryRejected{command.item.id, error});
        return;
    }
    auto slots = playerContainers_;
    slots.equipment = playerContainers_.hirelingEquipment;
    const auto before = hirelingStats();
    EquipmentActor actor{"", before.base.strength, before.base.dexterity, state().player.hireling.level};
    auto result = inventory_.equip(EquipItem{command.item, command.slot, command.destination},
                                   slots, inventoryAccess(), actor);
    const bool changed = bool(result);
    publishInventory(std::move(result), command.item.id);
    if (changed) simulation_.state_.player.hireling.hp = std::min(state().player.hireling.hp,
                                                               float(hirelingStats().base.life));
}
} // namespace d2x

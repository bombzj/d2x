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
    next.id = ids_.allocate();
    next.seed = offer.seed;
    next.combatRandom = childRandom(simulation_.unitRandom_);
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
    const auto bonus = std::max(0, 100 + hirelingStats().combat.experiencePercent);
    uint64_t amount = std::min(award.amount * unsigned(bonus) / 100,
                               (stats.nextExperience - stats.experience) >> 6);
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
unsigned GameSession::hirelingResurrectionCost() const {
    const auto &merc = state().player.hireling;
    return merc.sourceRow < 0 ? 0u : unsigned(std::min(50000, 15 * merc.level * merc.level / 2));
}
bool GameSession::canResurrectHireling(EntityId npc) const {
    const auto *target = object(npc);
    const auto &merc = state().player.hireling;
    return target && target->npcClass == "kashya" && content_.stashLayout.expansion &&
           merc.sourceRow >= 0 && !merc.active();
}
void GameSession::resurrectHireling(EntityId npc) {
    auto &player = simulation_.state_.player;
    if (engagedNpc_ != npc || player.dead || !region().definition.safe || !canResurrectHireling(npc)) return;
    const unsigned cost = hirelingResurrectionCost();
    if (uint64_t(player.gold) + player.bankGold < cost) {
        simulation_.emit(InteractionFailed{npc, "Not enough gold."});
        return;
    }
    const unsigned wallet = std::min(player.gold, cost);
    player.gold -= wallet; player.bankGold -= cost - wallet;
    const auto previous = player.hireling;
    HirelingState next;
    next.id = ids_.allocate(); next.sourceRow = previous.sourceRow; next.classId = previous.classId;
    next.nameKey = previous.nameKey; next.level = previous.level; next.seed = previous.seed;
    next.experience = previous.experience; next.combatRandom = previous.combatRandom;
    next.pos = player.pos;
    player.hireling = std::move(next);
    player.hireling.hp = float(hirelingStats().base.life);
    engagedNpc_ = {};
    simulation_.emit(HirelingHired{npc});
}
InventoryError GameSession::previewHirelingPotion(ItemHandle handle) const {
    if (auto error = inventory_.checkHandle(handle); error != InventoryError::None) return error;
    if (state().player.dead || !state().player.hireling.active()) return InventoryError::AccessDenied;
    const auto *item = inventory_.item(handle.id);
    const auto *location = std::get_if<ContainerLocation>(&item->location);
    if (!location || (location->container != playerContainers_.backpack &&
                      location->container != playerContainers_.belt &&
                      location->container != playerContainers_.cursor)) return InventoryError::AccessDenied;
    const auto *potion = content_.potion(item->definition);
    if (!potion || (potion->kind != PotionKind::Healing && potion->kind != PotionKind::Rejuvenation &&
                    potion->kind != PotionKind::Remedy)) return InventoryError::UnsupportedUse;
    if (location->container != playerContainers_.cursor)
        return inventory_.previewDrink(handle, inventoryAccess());
    return InventoryError::None;
}
void GameSession::useHirelingPotion(ItemHandle handle) {
    const auto error = previewHirelingPotion(handle);
    if (error != InventoryError::None) { simulation_.emit(InventoryRejected{handle.id, error}); return; }
    const auto &item = *inventory_.item(handle.id);
    const auto code = item.definition;
    const auto potion = *content_.potion(code);
    const bool cursor = std::get<ContainerLocation>(item.location).container == playerContainers_.cursor;
    auto result = cursor ? inventory_.consume(handle, 1, inventoryAccess()) : inventory_.drink(handle, inventoryAccess());
    const bool consumed = bool(result);
    publishInventory(std::move(result), handle.id);
    if (!consumed) return;
    auto &merc = simulation_.state_.player.hireling;
    const auto stats = hirelingStats();
    if (potion.kind == PotionKind::Healing) {
        // Items::GetBonusLifeBasedOnClass gives monsters the same x2 multiplier
        // as the loaded Barbarian amount. pSpell03 averages stacked recovery.
        int amount = int(potion.amount * 256.f);
        if (stats.vitality > 0) {
            const auto chance = limitedRandom(merc.combatRandom, 100);
            if (chance < limitedRandom(merc.combatRandom, unsigned(stats.vitality)) / 2) amount *= 2;
        }
        int frames = int(potion.seconds * 25.f + .5f);
        for (const auto &heal : merc.healing) {
            amount += int(heal.remaining * 256.f);
            frames += int(heal.remaining / heal.rate * 25.f + .5f);
        }
        merc.healing.clear();
        const float rate = float(amount / std::max(1, frames)) * 25.f / 256.f;
        if (rate > 0) merc.healing.push_back({rate * frames / 25.f, rate});
    } else if (potion.kind == PotionKind::Rejuvenation) {
        merc.hp = std::min(float(stats.base.life), merc.hp + stats.base.life * potion.amount);
    }
    if (potion.curesPoison) merc.poisonRemaining = merc.poisonPerSecond = 0;
    if (potion.curesCold) merc.chill = 0;
    if (potion.state.id >= 0) {
        for (int cured : potion.cureStates) if (cured >= 0) merc.combatEffects.removeState(cured);
        EffectFrame duration = potion.durationFrames;
        for (const auto &effect : merc.combatEffects.entries())
            if (effect.spec.state.id == potion.state.id && effect.expiresAt && *effect.expiresAt > state().frame)
                duration += *effect.expiresAt - state().frame;
        CombatEffectSpec effect;
        effect.state = potion.state; effect.source = {CombatEffectSource::Item, merc.id, potion.state.id, 0};
        effect.duration = duration; effect.modifiers = potion.modifiers;
        merc.combatEffects.apply(std::move(effect), state().frame);
    }
    simulation_.emit(ItemUsed{handle.id, code});
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
    actor.hireling = true;
    auto modifiers = resolveEquipmentModifiers(content_, inventory, slots, actor);
    mergeCharacterModifiers(modifiers, hireling.combatEffects.modifiers(state().frame));
    actor.strength += modifiers.strength; actor.dexterity += modifiers.dexterity;
    result.vitality = std::max(0, modifiers.vitality);
    result.base.strength = actor.strength; result.base.dexterity = actor.dexterity;
    result.base.life = std::max(1, (result.base.life + modifiers.maxLife) *
                                  (100 + modifiers.combat.lifePercent) / 100);
    auto combat = modifiers.combat;
    combat.minimumDamage += result.base.damageMin;
    combat.maximumDamage += result.base.damageMax;
    auto equipment = deriveEquipmentStats(inventory, slots, actor,
        result.base.defense + modifiers.defense, combat,
        result.base.attackRating + 5 * actor.dexterity + modifiers.attackRating);
    result.weapon = equipment.weapons[0];
    if (!result.weapon.item) {
        // MISSILE_CalculateDamageData: a hireling without a weapon uses dexterity,
        // its own secondary damage and equipment bonuses, not player fist damage.
        auto &weapon = result.weapon;
        weapon.ranged = true;
        weapon.projectileMinimum = std::max(0, combat.minimumDamage) * 256;
        weapon.projectileMaximum = std::max(combat.minimumDamage, combat.maximumDamage) * 256;
        weapon.projectileDamagePercent = std::max(-90, actor.dexterity + combat.damagePercent +
            std::max(combat.minimumDamagePercent, combat.maximumDamagePercent));
        weapon.minimum = int(int64_t(weapon.projectileMinimum) * (100 + weapon.projectileDamagePercent) / 100);
        weapon.maximum = int(int64_t(weapon.projectileMaximum) * (100 + weapon.projectileDamagePercent) / 100);
        weapon.fasterAttack = combat.fasterAttack;
    }
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
    const auto elements = attackElementRanges(result.combat, result.weapon.item);
    result.displayDamageMin = result.base.damageMin + elements.fire.minimum + elements.lightning.minimum +
        elements.cold.minimum + elements.magic.minimum;
    result.displayDamageMax = result.base.damageMax + elements.fire.maximum + elements.lightning.maximum +
        elements.cold.maximum + elements.magic.maximum;
    WeaponModifiers own;
    if (auto found = result.combat.weapons.find(result.weapon.item); found != result.combat.weapons.end()) own = found->second;
    const int frames = (result.combat.poisonFrames + own.poisonFrames) /
        std::max(1, result.combat.poisonSources + own.poisonSources);
    result.displayDamageMin += int(int64_t(result.combat.poisonMinimum + own.poisonMinimum) * frames / 256);
    result.displayDamageMax += int(int64_t(result.combat.poisonMaximum + own.poisonMaximum) * frames / 256);
    result.fasterMoveVelocity = modifiers.fasterMoveVelocity;
    result.velocityPercent = modifiers.velocityPercent;
    return result;
}
InventoryError GameSession::previewHirelingEquipment(const EquipHirelingItem &command) const {
    const auto *d = hirelingDefinition();
    if (!d || !state().player.hireling.active() || state().player.dead ||
        !map().activation.nearby(state().player.pos, state().player.hireling.pos))
        return InventoryError::AccessDenied;
    const auto *item = inventory_.item(command.item.id);
    if (!item) return InventoryError::UnknownItem;
    if (command.destination && !inventoryDestinationAllowed(*command.destination))
        return InventoryError::InvalidLocation;
    const auto &itemDefinition = *inventory_.catalog().find(item->definition);
    const auto &equipment = itemDefinition.equipment;
    if (command.slot && ((itemDefinition.maxDurability && !item->durability) ||
                         equipment.isType("ques"))) return InventoryError::RestrictedItem;
    if (command.slot && (*command.slot != EquipmentSlot::Head && *command.slot != EquipmentSlot::Torso &&
        *command.slot != EquipmentSlot::RightHand)) return InventoryError::RestrictedItem;
    if (command.slot == EquipmentSlot::RightHand &&
        !equipment.isType(d->weaponType1) && (d->weaponType2.empty() || !equipment.isType(d->weaponType2)))
        return InventoryError::RestrictedItem;
    auto slots = playerContainers_;
    slots.equipment = playerContainers_.hirelingEquipment;
    const auto actor = hirelingEquipmentActor(command.slot);
    return inventory_.preview(EquipItem{command.item, command.slot, command.destination},
                              slots, inventoryAccess(), actor);
}
EquipmentActor GameSession::hirelingEquipmentActor(std::optional<EquipmentSlot> replacedSlot) const {
    const auto &merc = state().player.hireling;
    const auto base = deriveHirelingStats(*hirelingDefinition(), merc.level);
    // PlrMsg::MERCS_EquipItem checks again after removing the replaced item.
    // Do not borrow the owner's charms or the outgoing item's requirement bonuses.
    PlayerContainers slots;
    slots.equipment = playerContainers_.hirelingEquipment;
    EquipmentActor actor{"", base.strength, base.dexterity, merc.level};
    actor.hireling = true;
    const auto excluded = replacedSlot ? inventory_.equipped(slots, *replacedSlot) : EntityId{};
    auto mods = resolveEquipmentModifiers(content_, inventory_, slots, actor, excluded);
    mergeCharacterModifiers(mods, merc.combatEffects.modifiers(state().frame));
    actor.strength += mods.strength;
    actor.dexterity += mods.dexterity;
    return actor;
}
void GameSession::equipHirelingItem(const EquipHirelingItem &command) {
    if (auto error = previewHirelingEquipment(command); error != InventoryError::None) {
        simulation_.emit(InventoryRejected{command.item.id, error});
        return;
    }
    auto slots = playerContainers_;
    slots.equipment = playerContainers_.hirelingEquipment;
    const auto actor = hirelingEquipmentActor(command.slot);
    auto result = inventory_.equip(EquipItem{command.item, command.slot, command.destination},
                                   slots, inventoryAccess(), actor);
    const bool changed = bool(result);
    publishInventory(std::move(result), command.item.id);
    if (changed) simulation_.state_.player.hireling.hp = std::min(state().player.hireling.hp,
                                                               float(hirelingStats().base.life));
}
} // namespace d2x

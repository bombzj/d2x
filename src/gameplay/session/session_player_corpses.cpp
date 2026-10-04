#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/geometry.hpp"
#include <algorithm>
#include <array>

namespace d2x {
void GameSessionImpl::settlePlayerDeath() {
    const bool died = std::any_of(simulation_->events_.begin(), simulation_->events_.end(),
        [&](const GameEvent &event) {
            const auto *death = std::get_if<PlayerDied>(&event);
            return death && death->player == state().player.id;
        });
    if (!died) return;
    auto &player = simulation_->state_.player;
    pendingCorpse_ = {}; cancelExit(); cancelPickup(); cancelInteraction(); closeStorage();
    PlayerCorpse corpse;
    corpse.owner = player.id; corpse.region = state().area.region;
    corpse.position = player.movement.pos; corpse.look = player.movement.look;
    // PlrModes.CORPSE_Handler accepts sixteen corpses. Further equipped/cursor
    // items fall to the ground; it does not overwrite an older corpse.
    const bool create = playerCorpses_.size() < 16;
    if (create) {
        corpse.id = ids_.allocate();
        corpse.items = inventory_.createContainer({player.id, ContainerKind::Corpse,
                                                    int(EquipmentSlot::Count) + 1, 1});
    }
    auto detached = inventory_.detachDeathEquipment(playerContainers_, corpse.items,
                                                      {corpse.region, corpse.position});
    if (!detached) {
        if (corpse.items) inventory_.state_.containers.erase(corpse.items);
        publishInventory(std::move(detached), {});
        simulation_->state_.message = "Death equipment could not be placed; your items were retained.";
        return;
    }
    publishInventory(std::move(detached), {});
    auto &character = player.character;
    // PLAYER_ApplyDeathPenalty, ordinary single player: bank gold is protected,
    // and the first 500 gold per level is exempt from the loss calculation.
    const uint64_t total = uint64_t(character.gold) + character.bankGold;
    const auto protectedGold = uint64_t(character.level) * 500;
    const auto penalty = std::min<uint64_t>(character.gold,
        std::min(total * unsigned(std::min(character.level, 20)) / 100,
                 total > protectedGold ? total - protectedGold : 0));
    character.gold -= unsigned(penalty);
    for (const auto &[code, definition] : inventory_.catalog().entries()) {
        if (!definition.equipment.isType("gold")) continue;
        while (character.gold) {
            auto amount = std::min(character.gold, definition.maxStack);
            if (!amount) break;
            auto dropped = inventory_.createItem(code, amount, GroundLocation{corpse.region, corpse.position},
                                                 1, {}, corpse.position);
            if (!dropped) {
                simulation_->emit(LootDeferred{player.id, "No free ground cell for death gold; it remains in the wallet."});
                break;
            }
            character.gold -= amount;
            publishInventory(std::move(dropped), {});
        }
        break;
    }
    const auto &thresholds = experienceThresholds();
    if (character.level > 1 && size_t(character.level + 1) < thresholds.size()) {
        const uint64_t floor = thresholds[size_t(character.level)];
        const auto loss = std::min(character.experience - floor,
            (thresholds[size_t(character.level + 1)] - floor) *
                unsigned(content_.playerDeath.experiencePenalty[size_t(state().population.difficulty)]) / 100);
        character.experience -= loss;
        corpse.recoverableExperience = loss * 75 / 100;
    }
    if (create) {
        player.actions.deathCorpse = corpse.id;
        playerCorpses_.push_back(corpse);
    }
}
bool GameSessionImpl::completePlayerDeathAnimation() {
    auto &player = simulation_->state_.player;
    if (!player.actions.dead) return false;
    if (player.actions.deathCompleted) return true;
    const auto death = content_.playerDeath.timings.find(characterAppearance() + "dthth");
    if (death == content_.playerDeath.timings.end()) {
        simulation_->state_.message = "Original death animation timing is unavailable.";
        return false;
    }
    if (player.actions.deathTime * 25 * death->second.speed / 256 < death->second.frames) return false;
    simulation_->finishPlayerDeathAnimation();
    return true;
}
bool GameSessionImpl::respawnPlayer() {
    if (!completePlayerDeathAnimation()) return false;
    auto &player = simulation_->state_.player;
    const auto level = worldContent_.levels().find(int(state().area.region));
    if (level == worldContent_.levels().end() || level->second.act < 0 || level->second.act >= 5) return false;
    const auto town = RegionId(actTownLevels[size_t(level->second.act)]);
    ensureRegion(town, true);
    // Do not restart the field: its monsters, drops and the corpse keep their state.
    pendingCorpse_ = {}; player.actions.dead = false; player.actions.deathTime = 0;
    player.actions.deathCompleted = false;
    player.actions.deathCorpse = {};
    enter(town);
    refreshCharacter();
    player.resources.hp = float(player.attributes.maxLife);
    player.resources.mana = float(player.attributes.maxMana);
    player.resources.stamina = float(player.attributes.maxStamina);
    simulation_->state_.message = "Recover your corpse to retrieve your equipment.";
    return true;
}
void GameSessionImpl::beginCorpseRecovery(EntityId id) {
    pendingCorpse_ = {};
    const auto found = std::find_if(playerCorpses_.begin(), playerCorpses_.end(),
        [id](const auto &corpse) { return corpse.id == id; });
    const auto &player = state().player;
    if (found == playerCorpses_.end() || found->owner != player.id ||
        found->region != state().area.region || player.actions.dead || cursorItem() ||
        meleeDistance(player.movement.pos, 2, found->position, 2) > 50) return;
    cancelExit(); cancelPickup(); cancelInteraction();
    simulation_->stopWalking();
    pendingCorpse_ = id;
    if (meleeDistance(player.movement.pos, 2, found->position, 2) > 8)
        simulation_->execute(MoveTo{found->position});
    updateCorpseRecovery();
}
void GameSessionImpl::updateCorpseRecovery() {
    if (!pendingCorpse_) return;
    const auto found = std::find_if(playerCorpses_.begin(), playerCorpses_.end(),
        [&](const auto &corpse) { return corpse.id == pendingCorpse_; });
    const auto &player = state().player;
    if (found == playerCorpses_.end() || found->region != state().area.region ||
        found->owner != player.id || player.actions.dead || cursorItem()) { pendingCorpse_ = {}; return; }
    // PlrMsg.sub_6FC828D0 uses the original unit distance, not a pixel radius.
    if (meleeDistance(player.movement.pos, 2, found->position, 2) <= 8) {
        pendingCorpse_ = {}; simulation_->stopWalking();
        recoverCorpse(*found);
    } else if (player.movement.route.empty()) {
        pendingCorpse_ = {};
        simulation_->emit(InteractionFailed{found->id, "Cannot reach your corpse."});
    }
}
void GameSessionImpl::recoverCorpse(PlayerCorpse &corpse) {
    const auto id = corpse.id;
    if (corpse.recoverableExperience) {
        const auto experience = corpse.recoverableExperience;
        corpse.recoverableExperience = 0;
        grantExperience(experience);
    }
    // Retry after each newly equipped attribute bonus, as ItemMode.sub_6FC4AD80
    // does. Never evict currently worn gear just to make room for corpse gear.
    bool progress;
    do {
        progress = false;
        for (int index = 0; index < int(EquipmentSlot::Count); ++index) {
            const auto item = inventory_.item(inventory_.itemAt(corpse.items, {index, 0}));
            if (!item) continue;
            const auto slot = EquipmentSlot(index);
            std::array<EquipmentSlot, 2> choices{slot, slot};
            switch (slot) {
            case EquipmentSlot::RightRing: choices[1] = EquipmentSlot::LeftRing; break;
            case EquipmentSlot::LeftRing: choices[1] = EquipmentSlot::RightRing; break;
            case EquipmentSlot::RightHand: choices[1] = EquipmentSlot::LeftHand; break;
            case EquipmentSlot::LeftHand: choices[1] = EquipmentSlot::RightHand; break;
            case EquipmentSlot::AlternateRightHand: choices[1] = EquipmentSlot::AlternateLeftHand; break;
            case EquipmentSlot::AlternateLeftHand: choices[1] = EquipmentSlot::AlternateRightHand; break;
            default: break;
            }
            const auto handle = item->handle();
            for (auto choice : choices) {
                auto result = inventory_.recoverCorpseItem(handle, playerContainers_, inventoryAccess(),
                                                           equipmentActor(), choice);
                if (!result) continue;
                publishInventory(std::move(result), {}); progress = true; break;
            }
        }
    } while (progress);
    for (auto item : inventory_.contents(corpse.items)) {
        const auto handle = inventory_.item(item)->handle();
        auto result = inventory_.recoverCorpseItem(handle, playerContainers_, inventoryAccess(),
                                                   equipmentActor(), {});
        if (result) publishInventory(std::move(result), {});
    }
    if (inventory_.contents(corpse.items).empty()) {
        inventory_.state_.containers.erase(corpse.items);
        std::erase_if(playerCorpses_, [id](const auto &entry) { return entry.id == id; });
        simulation_->state_.message = "Your corpse has been recovered.";
    } else {
        simulation_->state_.message = "Some items remain on your corpse. Make room and try again.";
        simulation_->emit(InteractionFailed{id, "Some items remain on your corpse."});
    }
}
} // namespace d2x

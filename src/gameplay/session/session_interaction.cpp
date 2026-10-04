#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include <algorithm>
#include <cmath>
#include "world/interaction_geometry.hpp"

namespace d2x {
namespace {
InteractionTarget targetGeometry(const WorldObject &object) {
    return {object.id, object.pos, object.accessPoint, object.collisionWidth,
        object.collisionHeight, object.reach,
        object.objectClass >= 0 && object.appearance.category == "objects"};
}
} // namespace
NpcAccess GameSessionImpl::npcAccess(EntityId npc) const {
    const auto *target = object(npc);
    return {state().player.id, target ? npc : EntityId{}, engagedNpc_, region().definition.id,
        !state().player.actions.dead, target && canReach(*target), region().definition.safe};
}
const WorldObject *GameSessionImpl::object(EntityId id) const {
    const auto &objects = region().objects;
    auto found = std::find_if(objects.begin(), objects.end(), [id](const auto &o) { return o.id == id; });
    return found == objects.end() || found->questHidden ? nullptr : &*found;
}
bool GameSessionImpl::canReach(const WorldObject &object) const {
    const auto &player = state().player;
    return !player.actions.dead && interactionClear(map().grid, player.movement.pos, targetGeometry(object));
}
std::optional<Vec> GameSessionImpl::interactionApproach(const WorldObject &object) const {
    return d2x::interactionApproach(map().grid, state().player.movement.pos, targetGeometry(object));
}
StorageAccess GameSessionImpl::storage() const {
    auto target = object(storage_.object);
    return target && target->interaction == Interaction::Stash && canReach(*target) ? storage_
                                                                                    : StorageAccess{};
}
InventoryAccess GameSessionImpl::inventoryAccess() const {
    const auto &player = state().player;
    InventoryAccess access{player.id, !player.actions.dead, region().definition.id, player.movement.pos, storage().container, 4, {}};
    if (playerContainers_.cube)
        for (auto id : inventory_.contents(playerContainers_.backpack))
            if (inventory_.item(id)->definition == content_.cubeCode) {
                access.portableContainer = playerContainers_.cube;
                break;
            }
    return access;
}
void GameSessionImpl::closeStorage() {
    if (storage_)
        simulation_->emit(StorageClosed{storage_.container});
    storage_ = {};
}
void GameSessionImpl::validateStorage() {
    if (storage_ && !storage())
        closeStorage();
}
void GameSessionImpl::cancelInteraction() {
    if (pendingInteraction_ || pendingPortal_)
        simulation_->stopWalking();
    pendingInteraction_ = {};
    pendingInteractionRepath_ = false;
    engagedNpc_ = {};
    pendingPortal_.reset();
    pendingCainPortal_ = false;
}
void GameSessionImpl::interact(EntityId id) {
    if (pendingInteraction_ == id)
        return;
    cancelInteraction();
    const auto *target = object(id);
    if (!target || target->questHidden || target->interaction == Interaction::None || state().player.actions.dead)
        return;
    if (storage_.object != id)
        closeStorage();
    pendingInteraction_ = id;
    simulation_->stopWalking();
    if (!canReach(*target)) {
        if (auto approach = interactionApproach(*target))
            simulation_->execute(MoveTo{*approach});
    }
    updateInteraction();
}
void GameSessionImpl::updateInteraction() {
    if (!pendingInteraction_)
        return;
    auto target = object(pendingInteraction_);
    const auto &player = state().player;
    if (!target || target->questHidden || player.actions.dead) {
        cancelInteraction();
        return;
    }
    if (player.actions.castTime > 0 || player.actions.meleeTime > 0)
        return;
    if (canReach(*target)) {
        cancelInteraction();
        completeInteraction(*target);
    } else if (player.movement.route.empty()) {
        if (!target->npcPath.empty() && !pendingInteractionRepath_) {
            pendingInteractionRepath_ = true;
            if (auto approach = interactionApproach(*target)) {
                simulation_->execute(MoveTo{*approach});
                if (!state().player.movement.route.empty())
                    return;
            }
        }
        simulation_->emit(InteractionFailed{target->id, "Cannot reach that object."});
        cancelInteraction();
    }
}
void GameSessionImpl::completeInteraction(const WorldObject &object) {
    if (object.isWaypoint()) {
        if (simulation_->state_.waypoints.emplace(region().definition.id, state().time).second) {
            simulation_->emit(WaypointActivated{object.id});
            return;
        }
    }
    switch (object.interaction) {
    case Interaction::QuestObject:
        activateLaterQuestObject(object.id);
        break;
    case Interaction::ActTwoQuest:
        activateActTwoObject(object.id);
        break;
    case Interaction::Stair: {
        auto &objects = world_.at(current_).objects;
        auto found = std::find_if(objects.begin(), objects.end(), [&](const auto &value) {
            return value.id == object.id;
        });
        if (found == objects.end()) break;
        const int mode = found->modeAt(state().time);
        if (mode == 0 && found->operateFn == 47) {
            found->operatedAt = state().time;
            simulation_->emit(ObjectInteracted{found->id, found->interaction, found->name});
        } else if (mode == 2) {
            const auto exit = std::find_if(region().exits.begin(), region().exits.end(),
                [&](const auto &value) { return value.stairObject == object.id; });
            if (exit != region().exits.end()) beginExit(exit->slot);
        }
        break;
    }
    case Interaction::TeleportPad: {
        const auto &map = region().map;
        const auto *sourceRoom = map.activation.room(object.pos);
        if (!sourceRoom) break;
        const auto near = map.activation.nearRooms(*sourceRoom);
        const WorldObject *destination = nullptr;
        for (const auto &candidate : region().objects) {
            if (candidate.id == object.id || candidate.objectClass != object.objectClass ||
                candidate.interaction != Interaction::TeleportPad)
                continue;
            const auto *candidateRoom = map.activation.room(candidate.pos);
            if (std::find(near.begin(), near.end(), candidateRoom) == near.end())
                continue;
            const bool sameRoom = candidateRoom == sourceRoom;
            const bool destinationSameRoom = destination && map.activation.room(destination->pos) == sourceRoom;
            if (!destination || (sameRoom && !destinationSameRoom) ||
                (sameRoom == destinationSameRoom &&
                 (candidate.pos - object.pos).length() < (destination->pos - object.pos).length()))
                destination = &candidate;
        }
        if (!destination) {
            simulation_->emit(InteractionFailed{object.id, "No linked teleport pad in nearby rooms."});
            break;
        }
        std::optional<Vec> landing;
        for (int radius = 0; radius <= 3 && !landing; ++radius)
            for (int vertical = -radius; vertical <= radius && !landing; ++vertical)
                for (int horizontal = -radius; horizontal <= radius; ++horizontal) {
                    Vec point = destination->pos + Vec{float(horizontal), float(vertical)};
                    if (map.grid.walkable(point, playerMovement)) {
                        landing = point;
                        break;
                    }
                }
        if (!landing) {
            simulation_->emit(InteractionFailed{object.id, "Teleport pad destination is blocked."});
            break;
        }
        cancelExit();
        cancelPickup();
        auto &player = simulation_->state_.player;
        player.movement.pos = player.movement.previous = *landing;
        player.movement.route.clear();
        simulation_->relocateCompanions(player.id, *landing);
        if (player.hireling.active()) {
            player.hireling.pos = *landing;
            player.hireling.route.clear();
            player.hireling.moving = false;
            player.hireling.attack.reset();
            player.hireling.attackTimer = player.hireling.thinkTimer = 0;
        }
        simulation_->emit(ObjectInteracted{object.id, object.interaction, object.name});
        break;
    }
    case Interaction::Door: {
        if (object.objectClass == 153 && int(region().definition.id) == 73) return;
        auto &objects = world_.at(current_).objects;
        auto found = std::find_if(objects.begin(), objects.end(), [&](const WorldObject &value) {
            return value.id == object.id;
        });
        if (found == objects.end() || (found->lastDoorOperation >= 0 &&
            state().time < found->lastDoorOperation + .5f)) break;
        const int mode = found->modeAt(state().time);
        if (mode != 0 && mode != 2) break; // Locked/special modes need their own original rules.
        if (mode == 2) {
            const int left = int(std::floor(found->pos.x)) - found->collisionWidth / 2;
            const int bottom = int(std::floor(found->pos.y)) - found->collisionHeight / 2;
            const auto occupies = [&](Vec pos) {
                const int x = int(std::floor(pos.x)), y = int(std::floor(pos.y));
                return x >= left && x < left + found->collisionWidth &&
                    y >= bottom && y < bottom + found->collisionHeight;
            };
            // Native door operation refuses to close on units/corpses.
            if (occupies(state().player.movement.pos) ||
                (state().player.hireling.active() && occupies(state().player.hireling.pos)) ||
                std::any_of(state().area.enemies.begin(), state().area.enemies.end(),
                    [&](const Enemy &enemy) { return occupies(enemy.pos); }) ||
                std::any_of(objects.begin(), objects.end(), [&](const WorldObject &other) {
                    return other.appearance.category == "monsters" && occupies(other.pos);
                })) break;
        }
        // OBJECTS_OperateFunction08_Door changes NU <-> ON directly.
        found->operatedAt = -1;
        found->animationMode = mode == 0 ? 2 : 0;
        found->lastDoorOperation = state().time;
        world_.at(current_).refreshObjectCollision(state().time);
        simulation_->emit(ObjectInteracted{found->id, Interaction::Door, found->name});
        break;
    }
    case Interaction::Stash:
        storage_ = {object.id, playerContainers_.stash};
        simulation_->emit(StorageOpened{object.id, playerContainers_.stash});
        break;
    case Interaction::Heal:
        simulation_->heal();
        [[fallthrough]];
    case Interaction::Talk: {
        if (introSpeech(content_.npcDialogues, object.name, {}, object.act) ||
            gossipSpeech(content_.npcDialogues, object.name, 0, object.act) || vendorStock(object.id) ||
            npcQuestDialogue(object.id).speech)
            engagedNpc_ = object.id;
        const auto *intro = introSpeech(content_.npcDialogues, object.name, state().player.character.characterClass, object.act);
        auto &introductions = simulation_->state_.player.character.npcIntroductions
            .at(size_t(state().population.difficulty));
        const auto introductionKey = npcIntroductionKey(object.name, object.act);
        const bool first = intro && introductions.insert(introductionKey).second;
        const auto dialogue = npcQuestDialogue(object.id);
        if (first) simulation_->emit(NpcDialogueStarted{object.id, object.name, intro->text});
        if (dialogue.automatic && dialogue.speech) {
            if (!first || dialogue.speech != intro)
                simulation_->emit(NpcDialogueStarted{object.id, object.name, dialogue.speech->text});
            talkToNpc(object.id);
        } else if (!first)
            simulation_->emit(ObjectInteracted{object.id, object.interaction, object.name});
        break;
    }
    case Interaction::Travel:
        simulation_->emit(ObjectInteracted{object.id, object.interaction, object.name});
        break;
    case Interaction::Loot:
        activateLootObject(object.id);
        break;
    case Interaction::Shrine:
        activateShrine(object.id);
        break;
    case Interaction::Well:
        drinkWell(object.id);
        break;
    case Interaction::QuestTree:
    case Interaction::QuestStone:
    case Interaction::QuestGibbet:
        activateCainQuestObject(object);
        break;
    case Interaction::QuestTome: {
        auto &record = simulation_->state_.player.character.quests
            .at(size_t(state().population.difficulty)).at(questIndex(QuestId::ForgottenTower));
        if (towerAdvance(record, TowerStage::TomeRead))
            simulation_->emit(QuestAdvanced{QuestId::ForgottenTower, record.stage});
        for (auto &candidate : world_.at(current_).objects)
            if (candidate.id == object.id) candidate.operatedAt = state().time;
        simulation_->emit(ObjectInteracted{object.id, object.interaction, object.name});
        break;
    }
    case Interaction::QuestMalus:
        activateMalus(object);
        break;
    case Interaction::None:
        break;
    }
}
void GameSessionImpl::unlockWaypoints() {
    if (state().player.actions.dead) return;
    for (const auto &region : world_.regions())
        for (const auto &object : region.objects)
            if (object.isWaypoint() &&
                simulation_->state_.waypoints.emplace(region.definition.id, state().time).second) {
                simulation_->emit(WaypointActivated{object.id});
                break;
            }
}
bool GameSessionImpl::travelWaypoint(const WaypointTravel &command) {
    const auto *source = object(command.source);
    if (!source || !source->isWaypoint() ||
        !canReach(*source) || !waypointUnlocked(region().definition.id) ||
        !waypointUnlocked(command.destination) || state().player.actions.castTime > 0 ||
        state().player.actions.meleeTime > 0) {
        simulation_->emit(InteractionFailed{command.source, "Waypoint unavailable or not activated."});
        return false;
    }
    ensureRegion(command.destination, true);
    for (const auto &destination : world_.regions())
        if (destination.definition.id == command.destination)
            for (const auto &target : destination.objects)
                if (target.isWaypoint()) {
                    if (command.destination == region().definition.id)
                        return false;
                    enter(command.destination, target.accessPoint);
                    return true;
                }
    simulation_->emit(InteractionFailed{command.source, "Destination waypoint is missing."});
    return false;
}
} // namespace d2x

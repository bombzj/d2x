#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
const WorldObject *GameSession::object(EntityId id) const {
    const auto &objects = region().objects;
    auto found = std::find_if(objects.begin(), objects.end(), [id](const auto &o) { return o.id == id; });
    return found == objects.end() ? nullptr : &*found;
}
bool GameSession::canReach(const WorldObject &object) const {
    const auto &player = state().player;
    return !player.dead && (player.pos - object.pos).length() <= object.reach &&
           (player.pos - object.accessPoint).length() <= object.reach &&
           map().grid.segment(player.pos, object.accessPoint);
}
StorageAccess GameSession::storage() const {
    auto target = object(storage_.object);
    return target && target->interaction == Interaction::Stash && canReach(*target) ? storage_
                                                                                    : StorageAccess{};
}
InventoryAccess GameSession::inventoryAccess() const {
    const auto &player = state().player;
    return {player.id, !player.dead, region().definition.id, player.pos, storage().container, 4};
}
void GameSession::closeStorage() {
    if (storage_)
        simulation_.emit(StorageClosed{storage_.container});
    storage_ = {};
}
void GameSession::validateStorage() {
    if (storage_ && !storage())
        closeStorage();
}
void GameSession::cancelInteraction() {
    if (pendingInteraction_ || pendingPortal_)
        simulation_.stopWalking();
    pendingInteraction_ = {};
    pendingPortal_.reset();
}
void GameSession::interact(EntityId id) {
    if (pendingInteraction_ == id)
        return;
    cancelInteraction();
    const auto *target = object(id);
    if (!target || target->interaction == Interaction::None || state().player.dead)
        return;
    if (storage_.object != id)
        closeStorage();
    pendingInteraction_ = id;
    simulation_.stopWalking();
    if (!canReach(*target))
        simulation_.execute(MoveTo{target->accessPoint});
    updateInteraction();
}
void GameSession::updateInteraction() {
    if (!pendingInteraction_)
        return;
    auto target = object(pendingInteraction_);
    const auto &player = state().player;
    if (!target || player.dead) {
        cancelInteraction();
        return;
    }
    if (player.castTime > 0 || player.meleeTime > 0 || player.leapTime > 0 || player.spinTime > 0)
        return;
    if (canReach(*target)) {
        cancelInteraction();
        completeInteraction(*target);
    } else if (player.route.empty()) {
        simulation_.emit(InteractionFailed{target->id, "Cannot reach that object."});
        cancelInteraction();
    }
}
void GameSession::completeInteraction(const WorldObject &object) {
    if (object.name == "Waypoint" && object.interaction == Interaction::Travel) {
        if (simulation_.state_.waypoints.emplace(region().definition.id, state().time).second) {
            simulation_.emit(WaypointActivated{object.id});
            return;
        }
    }
    switch (object.interaction) {
    case Interaction::Stash:
        storage_ = {object.id, playerContainers_.stash};
        simulation_.emit(StorageOpened{object.id, playerContainers_.stash});
        break;
    case Interaction::Heal:
        simulation_.heal();
        [[fallthrough]];
    case Interaction::Talk:
    case Interaction::Travel:
        simulation_.emit(ObjectInteracted{object.id, object.interaction, object.name});
        break;
    case Interaction::None:
        break;
    }
}
bool GameSession::travelWaypoint(const WaypointTravel &command) {
    const auto *source = object(command.source);
    if (!source || source->name != "Waypoint" || source->interaction != Interaction::Travel ||
        !canReach(*source) || !waypointUnlocked(region().definition.id) ||
        !waypointUnlocked(command.destination) || state().player.castTime > 0 ||
        state().player.meleeTime > 0 || state().player.leapTime > 0 || state().player.spinTime > 0) {
        simulation_.emit(InteractionFailed{command.source, "Waypoint unavailable or not activated."});
        return false;
    }
    for (const auto &destination : regions_)
        if (destination.definition.id == command.destination)
            for (const auto &target : destination.objects)
                if (target.name == "Waypoint" && target.interaction == Interaction::Travel) {
                    if (command.destination == region().definition.id)
                        return false;
                    enter(command.destination, target.accessPoint);
                    return true;
                }
    simulation_.emit(InteractionFailed{command.source, "Destination waypoint is missing."});
    return false;
}
} // namespace d2x

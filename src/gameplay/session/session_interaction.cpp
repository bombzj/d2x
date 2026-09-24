#include "gameplay/session/session.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

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
std::optional<Vec> GameSession::interactionApproach(const WorldObject &object) const {
    const auto &grid = map().grid;
    const Vec from = state().player.pos;
    std::optional<Vec> best;
    float bestCost = std::numeric_limits<float>::infinity();
    const int radius = int(std::ceil(object.reach));
    const int centerX = int(object.accessPoint.x), centerY = int(object.accessPoint.y);
    for (int y = centerY - radius; y <= centerY + radius; ++y)
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            if (!grid.walkable(x, y))
                continue;
            Vec candidate{x + .5f, y + .5f};
            if ((candidate - object.pos).length() > object.reach ||
                (candidate - object.accessPoint).length() > object.reach ||
                !grid.segment(candidate, object.accessPoint))
                continue;
            auto path = grid.path(from, candidate);
            if (path.empty() && (from - candidate).length() > .01f)
                continue;
            float cost = 0;
            Vec previous = from;
            for (auto step : path) {
                cost += (step - previous).length();
                previous = step;
            }
            if (cost < bestCost) {
                best = candidate;
                bestCost = cost;
            }
        }
    return best;
}
StorageAccess GameSession::storage() const {
    auto target = object(storage_.object);
    return target && target->interaction == Interaction::Stash && canReach(*target) ? storage_
                                                                                    : StorageAccess{};
}
InventoryAccess GameSession::inventoryAccess() const {
    const auto &player = state().player;
    InventoryAccess access{player.id, !player.dead, region().definition.id, player.pos, storage().container, 4, {}};
    if (playerContainers_.cube)
        for (auto id : inventory_.contents(playerContainers_.backpack))
            if (inventory_.item(id)->definition == content_.cubeCode) {
                access.portableContainer = playerContainers_.cube;
                break;
            }
    return access;
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
    pendingInteractionRepath_ = false;
    engagedNpc_ = {};
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
    if (!canReach(*target)) {
        if (target->npcPath.empty())
            simulation_.execute(MoveTo{target->accessPoint});
        else if (auto approach = interactionApproach(*target))
            simulation_.execute(MoveTo{*approach});
    }
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
        if (!target->npcPath.empty() && !pendingInteractionRepath_) {
            pendingInteractionRepath_ = true;
            if (auto approach = interactionApproach(*target)) {
                simulation_.execute(MoveTo{*approach});
                if (!state().player.route.empty())
                    return;
            }
        }
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
        if (introSpeech(content_.npcDialogues, object.name) || vendorStock(object.id))
            engagedNpc_ = object.id;
        simulation_.emit(ObjectInteracted{object.id, object.interaction, object.name});
        break;
    case Interaction::Travel:
        simulation_.emit(ObjectInteracted{object.id, object.interaction, object.name});
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
    case Interaction::None:
        break;
    }
}
void GameSession::unlockWaypoints() {
    if (state().player.dead) return;
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            if (object.name == "Waypoint" && object.interaction == Interaction::Travel &&
                simulation_.state_.waypoints.emplace(region.definition.id, state().time).second) {
                simulation_.emit(WaypointActivated{object.id});
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

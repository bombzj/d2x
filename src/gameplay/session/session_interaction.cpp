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
    if (pendingInteraction_)
        simulation_.stopWalking();
    pendingInteraction_ = {};
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
} // namespace d2x

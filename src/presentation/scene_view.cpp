#include "scene_view.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
SceneView::SceneView(Archives &archives, const GameSession &session)
    : session_(session), assets_(archives, session), painter_(assets_.font) {
    view_.camera = project(session_.state().player.pos);
}
Vec SceneView::screen(Vec p) const {
    float centerX = view_.inventory.storage ? W * .5f
                    : view_.inventory.open  ? inventoryBounds().x * .5f
                                            : W * .5f;
    return (project(p) - view_.camera) * view_.zoom + Vec{centerX, (H - HUD) * .5f};
}
Vec SceneView::world(Vec p) const {
    float centerX = view_.inventory.storage ? W * .5f
                    : view_.inventory.open  ? inventoryBounds().x * .5f
                                            : W * .5f;
    return unproject((p - Vec{centerX, (H - HUD) * .5f}) * (1 / view_.zoom) + view_.camera);
}
std::string playerAnimationMode(const PlayerState &p) {
    return p.dead                              ? "dt"
           : p.leapTime > 0                    ? "a1"
           : p.hitTime > 0 && p.spinTime <= 0  ? "gh"
           : p.castTime > 0                    ? "sc"
           : p.spinTime > 0 || p.meleeTime > 0 ? "a1"
           : p.moving                          ? (p.running ? "rn" : "wl")
                                               : "nu";
}
bool SceneView::visible(const WorldObject &object) const {
    auto found = assets_.propAnimations.find(object.key);
    return found != assets_.propAnimations.end() && !found->second.frames.empty();
}
void SceneView::notice(std::string text, bool error) {
    view_.lootNotice = std::move(text);
    view_.noticeError = error;
    view_.noticeTime = 4;
}
void SceneView::sessionRestored() {
    assets_.loadInventoryArt(session_.inventory());
    view_.inventory = {};
    view_.travelMenu = view_.help = false;
    view_.skillPicker.reset();
    view_.dialogue.clear();
    view_.camera = project(session_.state().player.pos);
    view_.clickAge = 10;
    view_.animationTime = view_.heroTime = view_.stepClock = 0;
    view_.heroMode = playerAnimationMode(session_.state().player);
    landingAge_.clear();
}
void SceneView::advance(float dt) {
    view_.noticeTime = std::max(0.f, view_.noticeTime - dt);
    for (auto &[id, age] : landingAge_)
        age += dt;
    std::erase_if(landingAge_, [](const auto &pair) { return pair.second > 4; });
    for (const auto &event : session_.events()) {
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, SkillCast>) {
                    view_.heroTime = 0;
                    assets_.audio.play(std::to_string(int(value.skill)));
                } else if constexpr (std::is_same_v<T, MeleeAttack>) {
                    view_.heroTime = 0;
                    assets_.audio.play("swing");
                } else if constexpr (std::is_same_v<T, EnemyDied>)
                    assets_.audio.play("impact");
                else if constexpr (std::is_same_v<T, RegionEntered>) {
                    view_.skillPicker.reset();
                    view_.inventory.cancelGesture();
                    view_.inventory.pending = {};
                    view_.inventory.open = false;
                    view_.inventory.storage = {};
                    landingAge_.clear();
                    view_.noticeTime = 0;
                    view_.camera = project(session_.state().player.pos);
                    view_.dialogue.clear();
                    view_.clickAge = 10;
                    view_.heroTime = 0;
                    view_.travelMenu = false;
                } else if constexpr (std::is_same_v<T, PlayerDied>) {
                    view_.skillPicker.reset();
                    view_.inventory.cancelGesture();
                    view_.inventory.open = false;
                    view_.inventory.storage = {};
                } else if constexpr (std::is_same_v<T, StorageOpened>) {
                    auto &ui = view_.inventory;
                    ui.cancelGesture();
                    ui.open = true;
                    ui.storage = value.container;
                    view_.dialogue.clear();
                    view_.travelMenu = view_.help = false;
                    view_.clickAge = 10;
                } else if constexpr (std::is_same_v<T, StorageClosed>) {
                    if (view_.inventory.storage == value.container) {
                        view_.inventory.storage = {};
                        view_.inventory.cancelGesture();
                    }
                } else if constexpr (std::is_same_v<T, InteractionFailed>) {
                    notice(value.reason, true);
                } else if constexpr (std::is_same_v<T, ItemUsed>) {
                    assets_.audio.play("drink");
                    const auto *def = session_.inventory().catalog().find(value.definition);
                    notice("Used: " + (def ? def->name : value.definition), false);
                } else if constexpr (std::is_same_v<T, BeltEquipped>) {
                    assets_.audio.play("belt");
                } else if constexpr (std::is_same_v<T, InventoryApplied>) {
                    auto &ui = view_.inventory;
                    if (ui.pending == value.requested) {
                        ui.pending = {};
                        ui.selected = value.item;
                        notice(value.transferred ? ui.pendingMessage : "Item is already there.", false);
                    }
                } else if constexpr (std::is_same_v<T, InventoryRejected>) {
                    if (view_.inventory.pending == value.item)
                        view_.inventory.pending = {};
                    const auto *item = session_.inventory().item(value.item);
                    bool ground = item && std::holds_alternative<GroundLocation>(item->location);
                    notice(value.error == InventoryError::NoSpace && ground
                               ? "Not enough room. Item stays on the ground."
                               : inventoryErrorText(value.error),
                           true);
                } else if constexpr (std::is_same_v<T, PickupFailed>) {
                    notice(value.reason, true);
                } else if constexpr (std::is_same_v<T, ItemPickedUp>) {
                    const auto *definition = session_.inventory().catalog().find(value.definition);
                    auto name = definition ? definition->name : value.definition;
                    if (value.quantity > 1)
                        name += " x" + std::to_string(value.quantity);
                    notice("Picked up: " + name, false);
                } else if constexpr (std::is_same_v<T, ItemChange>) {
                    if (value.kind == ItemChangeKind::Created)
                        assets_.loadInventoryArt(session_.inventory());
                    landingAge_.erase(value.item);
                    if (value.after)
                        if (auto ground = std::get_if<GroundLocation>(&*value.after);
                            ground && ground->region == session_.region().definition.id &&
                            (value.kind == ItemChangeKind::Created || value.kind == ItemChangeKind::Moved))
                            landingAge_[value.item] = 0;
                } else if constexpr (std::is_same_v<T, ObjectInteracted>) {
                    if (value.interaction == Interaction::Travel)
                        view_.travelMenu = true;
                    else if (value.interaction == Interaction::Heal)
                        view_.dialogue = "Akara: Your wounds are healed. Go in peace.";
                    else if (value.interaction == Interaction::Stash)
                        view_.dialogue = "Private Stash";
                    else
                        view_.dialogue = value.name + ": Welcome, traveler. The wilderness awaits.";
                }
            },
            event);
    }
    const auto &player = session_.state().player;
    auto inBackpack = [&](EntityId id) {
        auto *item = session_.inventory().item(id);
        auto location = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
        return location && (location->container == session_.playerContainers().backpack ||
                            location->container == session_.playerContainers().belt ||
                            location->container == session_.playerContainers().beltEquipment ||
                            location->container == session_.playerContainers().equipment ||
                            location->container == view_.inventory.storage);
    };
    if (!inBackpack(view_.inventory.selected))
        view_.inventory.selected = {};
    view_.animationTime += dt;
    view_.heroTime += dt;
    auto mode = playerAnimationMode(player);
    if (mode != view_.heroMode) {
        view_.heroMode = mode;
        view_.heroTime = 0;
    }
    view_.camera = view_.camera + (project(player.pos) - view_.camera) * std::min(1.f, dt * 10);
    view_.clickAge += dt;
    view_.stepClock -= dt;
    if (player.moving && view_.stepClock <= 0) {
        assets_.audio.play("step");
        view_.stepClock = player.running ? .28f : .42f;
    }
}
} // namespace d2x

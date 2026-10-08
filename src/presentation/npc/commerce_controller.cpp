#include "client/actor_client.hpp"
#include "client/npc_client.hpp"
#include "presentation/controller.hpp"
#include "presentation/scene_view.hpp"
#include "presentation/npc/hireling_panel.hpp"
#include <algorithm>

namespace d2x {
bool SceneController::handleHirelingList(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.hireListOpen) {
        const auto *offers = &view_.hirelingListView().offers;
        const auto cancel = hirelingListCancel();
        if (input.escape || (input.insideViewport && input.leftPressed &&
            CheckCollisionPointRec(rv(input.mouse), cancel))) {
            ui.hireListOpen = false;
            npcClient_.submit(EndNpcConversation{ui.dialogueObject});
            return true;
        }
        const int maximum = offers ? std::max(0, int(offers->size()) - hirelingVisibleRows) : 0;
        ui.hireListScroll = std::clamp(ui.hireListScroll + input.pageDelta - input.quantityDelta, 0, maximum);
        if (input.insideViewport && input.leftPressed) {
            const auto bar = hirelingScrollBounds();
            if (CheckCollisionPointRec(rv(input.mouse), bar)) {
                const float local = (input.mouse.y - bar.y) / classicPanelScale;
                if (local < 10) --ui.hireListScroll;
                else if (local > 320) ++ui.hireListScroll;
                else ui.hireListScroll = int(std::clamp((local - 10) / 310.f, 0.f, 1.f) * maximum + .5f);
                ui.hireListScroll = std::clamp(ui.hireListScroll, 0, maximum);
            } else if (offers) for (int row = 0; row < hirelingVisibleRows; ++row) {
                const int index = ui.hireListScroll + row;
                if (index >= int(offers->size())) break;
                if (CheckCollisionPointRec(rv(input.mouse), hirelingListRow(row))) {
                    npcClient_.submit(HireMercenary{ui.dialogueObject, offers->at(size_t(index)).slot});
                    break;
                }
            }
        }
        return true;
    }
    return false;
}

bool SceneController::handleNpcShop(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.shopOpen) {
        auto repairItemAt = [&]() -> const InventoryItemView * {
            const auto &inventory = view_.inventoryView();
            EntityId id;
            if (auto cell = inventoryCell(input.mouse))
                id = inventory.itemAt(view_.inventoryView().containers.backpack, *cell);
            if (auto slot = equipmentAt(input.mouse, view_.inventoryView().weaponSet))
                id = inventory.equipped(view_.inventoryView().containers, *slot);
            return inventory.item(id);
        };
        if (ui.shopSalePending) return true;
        if (input.escape) {
            if ((ui.inventory.drag && !ui.inventory.drag->onCursor) || ui.inventory.identify || ui.inventory.split || ui.inventory.goldDialog)
                ui.inventory.cancelGesture();
            else if (ui.shopRepair) ui.shopRepair = false;
            else if (ui.shopConfirm)
                ui.shopConfirm.reset();
            else {
                view_.closeNpcShop();
                npcClient_.submit(EndNpcConversation{ui.dialogueObject});
            }
        } else if (input.inventory || (!ui.shopConfirm && !ui.inventory.split && !ui.inventory.goldDialog &&
                   input.insideViewport && input.leftPressed &&
                   CheckCollisionPointRec(rv(input.mouse), inventoryClose()))) {
            view_.closeNpcShop();
            npcClient_.submit(EndNpcConversation{ui.dialogueObject});
        } else if (ui.inventory.drag) {
            const auto *source = view_.inventoryView().item(ui.inventory.drag->item.id);
            if (!source || source->revision != ui.inventory.drag->item.revision || input.rightPressed)
                return handleInventory(input);
            if ((input.mouse - ui.inventory.drag->pressedAt).length() > 4)
                ui.inventory.drag->moved = true;
            const bool drop = ui.inventory.drag->pickedUp ? input.leftPressed :
                input.leftReleased && ui.inventory.drag->moved;
            if (input.insideViewport && CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
                if (drop) {
                    ui.inventory.drag->pickedUp = ui.inventory.drag->moved = true;
                    inventoryClick_ = true;
                    if (view_.npcShopDropAt(input.mouse)) {
                        if (npcClient_.canRequestSale(ui.dialogueObject, source->handle())) {
                            ui.shopSalePending = source->handle();
                            npcClient_.submit(SellVendorItem{ui.dialogueObject, source->handle()});
                        } else view_.notice("That item cannot be sold here.", true);
                    }
                }
                return true;
            }
            return handleInventory(input);
        } else if (ui.inventory.identify || ui.inventory.split || ui.inventory.goldDialog) {
            return handleInventory(input);
        } else {
            if (input.pageDelta) view_.scrollNpcShop(-input.pageDelta);
            if (!ui.shopConfirm && input.insideViewport && input.leftPressed &&
                       view_.inventoryView().stashLayout.expansion &&
                       weaponTabAt(input.mouse).has_value()) {
                if (*weaponTabAt(input.mouse) != view_.inventoryView().weaponSet)
                    inventoryClient_.submit(SwitchWeaponSet{});
            } else if (input.enter && ui.shopConfirm) {
                const auto slot = *ui.shopConfirm;
                ui.shopConfirm.reset();
                npcClient_.submit(BuyVendorItem{ui.dialogueObject, slot, ui.shopGamble});
            } else if (!ui.shopConfirm && input.insideViewport &&
                       (inventorySurface(ui.inventory, input.mouse) ||
                        CheckCollisionPointRec(rv(input.mouse), beltBounds(
                            view_.inventoryView().container(view_.inventoryView().containers.belt)->rows)))) {
                if (ui.shopRepair && input.leftPressed) {
                    if (const auto *item = repairItemAt())
                        npcClient_.submit(RepairVendorItem{ui.dialogueObject, item->handle()});
                } else return handleInventory(input);
            } else if (input.insideViewport && input.leftPressed) {
                if (view_.npcShopRepairAllAt(input.mouse)) {
                    npcClient_.submit(RepairVendorItem{ui.dialogueObject, {}});
                    return true;
                }
                if (auto slot = view_.clickNpcShop(input.mouse))
                    npcClient_.submit(BuyVendorItem{ui.dialogueObject, *slot, ui.shopGamble,input.shift && !ui.shopGamble});
                if (!ui.shopOpen)
                    npcClient_.submit(EndNpcConversation{ui.dialogueObject});
            } else if (input.insideViewport && input.rightPressed) {
                inventoryRight_ = true;
                if (auto slot = view_.clickNpcShop(input.mouse, true))
                    npcClient_.submit(BuyVendorItem{ui.dialogueObject, *slot, ui.shopGamble,input.shift && !ui.shopGamble});
            }
        }
        return true;
    }
    return false;
}

bool SceneController::handleHirelingToggle(const FrameInput &input) {
    auto &ui = view_.ui();
    if (input.hireling && !ui.capturesWorldInput()) {
        if (!view_.hirelingView().active) {
            view_.notice("Native hireling state is not available yet.", true); return true;
        }
        ui.hirelingOpen = !ui.hirelingOpen && view_.hirelingView().active;
        if (ui.hirelingOpen) {
            if (ui.inventory.storage || ui.inventory.cubeOpen) toggleInventory();
            ui.characterOpen = ui.questOpen = false;
            ui.inventory.cancelGesture();
        }
        return true;
    }
    return false;
}

bool SceneController::handleHirelingPortrait(const FrameInput &input) {
    auto &ui = view_.ui();
    if (view_.hirelingPortraitVisible() && input.insideViewport &&
        (CheckCollisionPointRec(rv(input.mouse), hirelingPortraitBounds()) ||
         CheckCollisionPointRec(rv(input.mouse), hirelingLifeBounds()))) {
        cancelWorldGesture();
        if (ui.inventory.drag) {
            const auto drag = *ui.inventory.drag;
            const bool drop = drag.pickedUp ? input.leftPressed : input.leftReleased;
            if (drop && queueInventory(UseHirelingPotion{drag.item}, drag.item.id)) ui.inventory.drag.reset();
        } else if (input.leftPressed && !ui.inventory.identify && !ui.inventory.pending) {
            ui.hirelingOpen = true;
            ui.characterOpen = ui.questOpen = false;
            actorClient_.stopActions();
            pickupClick_ = true;
        }
        return true;
    }
    return false;
}

bool SceneController::handleHirelingPanel(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.hirelingOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (input.leftPressed && CheckCollisionPointRec(rv(input.mouse), hirelingClose())) {
            ui.hirelingOpen = false;
            ui.inventory.cancelGesture();
            return true;
        }
        if (ui.inventory.drag) return handleInventory(input);
        if (!ui.inventory.pending && input.leftPressed) {
            constexpr EquipmentSlot order[] = {EquipmentSlot::Head, EquipmentSlot::Torso,
                                               EquipmentSlot::RightHand, EquipmentSlot::RightHand};
            for (size_t index = 0; index < 4; ++index) {
                if (!CheckCollisionPointRec(rv(input.mouse), view_.hirelingSlotBounds(index))) continue;
                auto slots = view_.inventoryView().containers; slots.equipment = slots.hirelingEquipment;
                const auto id = view_.inventoryView().equipped(slots, order[index]);
                const auto *item = view_.inventoryView().item(id);
                if (item) {
                    queueInventory(EquipHirelingItem{item->handle(), std::nullopt,
                        ContainerLocation{slots.cursor, {}}}, id);
                }
                break;
            }
        }
        return true;
    }
    return false;
}
} // namespace d2x

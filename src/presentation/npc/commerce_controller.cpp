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
        const auto &list = view_.hirelingListView();
        if (list.npc == ui.dialogueObject && list.hired) {
            ui.hireListOpen = false;
            ui.hireListSlider = {};
            ui.hirelingConfirmation.reset();
            ui.npcMenu = view_.npcView().valid && view_.npcView().npc == ui.dialogueObject;
            return true;
        }
        if (list.hirePending) return true;
        if (const auto confirmation = ui.hirelingConfirmation) {
            if (list.npc != ui.dialogueObject || list.offerRevision != confirmation->offerRevision || input.escape) {
                ui.hirelingConfirmation.reset();
            } else if (input.insideViewport && input.leftPressed) {
                const auto choice = view_.hirelingConfirmationAt(input.mouse);
                if (choice >= 0) ui.hirelingConfirmation.reset();
                if (choice == 0) npcClient_.submit(HireMercenary{ui.dialogueObject, confirmation->slot});
            }
            return true;
        }
        const auto &offers = list.offers;
        const auto cancel = ui.hireListCancelButton.handle(view_.hirelingCancelBounds(), input);
        if (input.escape || cancel == ClassicButtonResult::Activated) {
            ui.hireListOpen = false;
            ui.hireListSlider = {};
            ui.hireListCancelButton = {};
            ui.npcMenu = view_.npcView().valid && view_.npcView().npc == ui.dialogueObject;
            return true;
        }
        if (cancel == ClassicButtonResult::Captured) {
            ui.hireListPressed.reset();
            return true;
        }
        const auto &layout = view_.hirelingListLayout();
        const int maximum = layout.lastPageFirst();
        ui.hireListScroll = std::clamp(ui.hireListScroll, 0, maximum);
        const auto slider = TextSlider::list(hirelingListContentBounds(), false);
        if (slider.handle(input, ui.hireListSlider, ui.hireListScroll, maximum, int(layout.rows.size()),
                          layout.pageSize(ui.hireListScroll))) {
            ui.hireListPressed.reset();
            return true;
        }
        if (input.focused && input.insideViewport && CheckCollisionPointRec(rv(input.mouse), hirelingListBounds()))
            ui.hireListScroll = std::clamp(ui.hireListScroll + input.pageDelta - input.quantityDelta, 0, maximum);
        std::optional<uint32_t> hovered;
        if (input.insideViewport && list.npc == ui.dialogueObject) {
            const int row = layout.hit(ui.hireListScroll, input.mouse);
            if (row >= 0) hovered = offers[size_t(layout.rows[size_t(row)].offer)].slot;
        }
        if (!input.focused || (ui.hireListPressed && ui.hireListPressed->offerRevision != list.offerRevision))
            ui.hireListPressed.reset();
        if (input.focused && input.leftPressed) {
            ui.hireListPressed.reset();
            if (hovered) ui.hireListPressed = ViewState::HirelingConfirmation{*hovered, list.offerRevision};
        }
        if (input.leftReleased) {
            const auto pressed = ui.hireListPressed;
            ui.hireListPressed.reset();
            if (pressed && hovered == pressed->slot) {
                if (list.replacing) ui.hirelingConfirmation = pressed;
                else npcClient_.submit(HireMercenary{ui.dialogueObject, pressed->slot});
            }
        }
        return true;
    }
    return false;
}

bool SceneController::handleNpcShop(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.shopOpen) {
        if (view_.handleNpcShopRejection(input.mouse, input.enter || input.escape,
            input.insideViewport && input.leftPressed)) return true;
        auto repairItemAt = [&]() -> const InventoryItemView * {
            const auto &inventory = view_.inventoryView();
            EntityId id;
            if (auto cell = inventoryCell(inventory, input.mouse))
                id = inventory.itemAt(view_.inventoryView().containers.backpack, *cell);
            if (auto slot = equipmentAt(inventory, input.mouse, inventory.weaponSet))
                id = inventory.equipped(view_.inventoryView().containers, *slot);
            return inventory.item(id);
        };
        if (ui.shopSalePending) return true;
        if (input.escape) {
            if (ui.inventory.identify || ui.inventory.goldDialog)
                ui.inventory.cancelGesture();
            else if (ui.shopRepair || ui.shopSell) ui.shopRepair = ui.shopSell = false;
            else if (ui.shopConfirm)
                ui.shopConfirm.reset();
            else {
                view_.closeNpcShop();
                npcClient_.submit(EndNpcConversation{ui.dialogueObject});
            }
        } else if (input.inventory || (!ui.shopConfirm && !ui.inventory.goldDialog &&
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
        } else if (ui.inventory.identify || ui.inventory.goldDialog) {
            return handleInventory(input);
        } else {
            if (ui.shopConfirm && input.menuDelta)
                ui.shopConfirmChoice = std::clamp(ui.shopConfirmChoice+input.menuDelta,0,1);
            if (!ui.shopConfirm && input.pageDelta) view_.scrollNpcShop(-input.pageDelta);
            if (!ui.shopConfirm && input.insideViewport && input.leftPressed &&
                       view_.inventoryView().stashLayout.expansion &&
                       weaponTabAt(input.mouse).has_value()) {
                if (*weaponTabAt(input.mouse) != view_.inventoryView().weaponSet)
                    inventoryClient_.submit(SwitchWeaponSet{});
            } else if (input.enter && ui.shopConfirm) {
                const auto slot = *ui.shopConfirm;
                ui.shopConfirm.reset();
                if (!ui.shopConfirmChoice)
                    npcClient_.submit(BuyVendorItem{ui.dialogueObject, slot, ui.shopGamble});
            } else if (!ui.shopConfirm && input.insideViewport &&
                       (inventorySurface(ui.inventory, input.mouse) ||
                        CheckCollisionPointRec(rv(input.mouse), beltBounds(
                            view_.inventoryView().container(view_.inventoryView().containers.belt)->rows)))) {
                if ((input.control || ui.shopSell) && input.leftPressed && !ui.shopRepair) {
                    if (const auto *item = repairItemAt(); item &&
                        npcClient_.canRequestSale(ui.dialogueObject,item->handle())) {
                        ui.shopSalePending = item->handle();
                        npcClient_.submit(SellVendorItem{ui.dialogueObject,item->handle()});
                    }
                } else if (ui.shopRepair && input.leftPressed) {
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
        ui.hirelingCloseButton = {};
        if (ui.hirelingOpen) {
            if (ui.inventory.storage || ui.inventory.cubeOpen) toggleInventory();
            ui.partyOpen = ui.characterOpen = ui.questOpen = false;
            ui.inventory.cancelGesture();
        }
        return true;
    }
    return false;
}

bool SceneController::handleHirelingPortrait(const FrameInput &input, float elapsed) {
    auto &ui = view_.ui();
    if (view_.hirelingPortraitVisible() && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), hirelingPortraitHitBounds(view_.partyPortraitOffset()))) {
        const auto &portraits=view_.hirelingView().portraits;
        const auto merc=std::find_if(portraits.begin(),portraits.end(),[](const auto &entry){return entry.hireling;});
        if(merc==portraits.end()) return false;
        if(ui.hirelingPortraitHovered!=merc->single) {ui.hirelingPortraitHovered=merc->single;ui.hirelingPortraitHoverTime=0;}
        else ui.hirelingPortraitHoverTime=std::min(hirelingPortraitHoverSeconds,ui.hirelingPortraitHoverTime+std::max(0.f,elapsed));
        ui.hirelingPortraitCanFeed=ui.inventory.drag &&
            inventoryClient_.preview(UseHirelingPotion{ui.inventory.drag->item})==InventoryError::None;
        cancelWorldGesture();
        inventoryClick_|=input.leftPressed;
        inventoryRight_|=input.rightPressed;
        if (ui.inventory.drag) {
            const auto drag = *ui.inventory.drag;
            const bool drop = drag.pickedUp ? input.leftPressed : input.leftReleased;
            if (drop && queueInventory(UseHirelingPotion{drag.item}, drag.item.id)) ui.inventory.drag.reset();
        } else if (input.rightPressed && ui.hirelingPortraitHoverTime>=hirelingPortraitHoverSeconds &&
            view_.hirelingView().active && !ui.inventory.identify && !ui.inventory.pending &&
            !ui.inventory.goldDialog) {
            ui.hirelingOpen = true;
            ui.partyOpen = ui.characterOpen = ui.questOpen = false;
            ui.skillPicker.reset();
            actorClient_.stopActions();
            ui.hirelingPortraitHovered={};ui.hirelingPortraitHoverTime=0;ui.hirelingPortraitCanFeed=false;
        }
        return true;
    }
    return false;
}

bool SceneController::handleHirelingPanel(const FrameInput &input) {
    auto &ui = view_.ui();
    if (!ui.hirelingOpen) return false;
    const auto close = ui.hirelingCloseButton.handle(hirelingCloseHit(), input);
    if (close == ClassicButtonResult::Activated) {
        ui.hirelingOpen = false;
        ui.inventory.cancelGesture();
    }
    if (close != ClassicButtonResult::None) return true;
    if (input.insideViewport && CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (ui.inventory.drag) return handleInventory(input);
        if (!ui.inventory.pending && input.leftPressed) {
            const auto &order = hirelingEquipmentOrder;
            for (size_t index = 0; index < order.size(); ++index) {
                if (view_.inventoryView().hirelingSlots[index][0] < 0) continue;
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

#include "remote_ui_clients_impl.hpp"

namespace d2x {
const InventoryView &RemoteUiClients::Impl::Inventory::read() const { return o.inventoryView; }
bool RemoteUiClients::Impl::Inventory::selectStoragePage(unsigned page) {
    if (!onlinePd2ItemAction(OnlineItemAction::StoragePage) ||
        !o.inventoryView.personalItemActionsOnly || !o.inventoryView.storagePagingAvailable || o.inventoryView.readOnly || o.inventoryView.dead ||
        !o.transactions.empty() || o.waiting || !onlineWorldMatches(o.context, o.session.read())) return false;
    OnlineItemCommand command;
    command.action = OnlineItemAction::StoragePage; command.page = page;
    command.sharedStash = o.inventoryView.storageShared;
    command.context = onlineIntentContext(o.session.read());
    if (!o.items.submit(o.session, command)) { o.notice = o.items.reason(); return false; }
    o.context = onlineIntentContext(o.session.read());
    return true;
}
bool RemoteUiClients::Impl::Inventory::selectSharedStorage(bool shared) {
    if (!o.inventoryView.personalItemActionsOnly || !o.inventoryView.storagePagingAvailable ||
        o.inventoryView.readOnly || o.inventoryView.dead || !o.transactions.empty() || o.waiting ||
        !onlineWorldMatches(o.context, o.session.read())) return false;
    OnlineItemCommand command;
    command.action = OnlineItemAction::StoragePage; command.page = 1; command.sharedStash = shared;
    command.context = onlineIntentContext(o.session.read());
    if (!o.items.submit(o.session, command)) { o.notice = o.items.reason(); return false; }
    o.context = onlineIntentContext(o.session.read());
    return true;
}
bool RemoteUiClients::Impl::Inventory::submitStorage(OnlineItemCommand command) {
    if (!o.inventoryView.personalItemActionsOnly || o.inventoryView.readOnly || o.inventoryView.dead ||
        !o.transactions.empty() || o.waiting || !onlineWorldMatches(o.context, o.session.read())) return false;
    command.context = onlineIntentContext(o.session.read());
    if (!o.items.submit(o.session, command)) { o.notice = o.items.reason(); return false; }
    o.context = onlineIntentContext(o.session.read());
    return true;
}
bool RemoteUiClients::Impl::Inventory::navigateStorage(StorageNavigation navigation) {
    OnlineItemCommand command; command.action = OnlineItemAction::StoragePage;
    command.sharedStash = o.inventoryView.storageShared;
    switch (navigation) {
    case StorageNavigation::PreviousIndex: command.stashNavigation = OnlineCnStashNavigation::PreviousIndex; break;
    case StorageNavigation::NextIndex: command.stashNavigation = OnlineCnStashNavigation::NextIndex; break;
    case StorageNavigation::First: command.stashNavigation = OnlineCnStashNavigation::First; break;
    case StorageNavigation::LastNonempty: command.stashNavigation = OnlineCnStashNavigation::LastNonempty; break;
    case StorageNavigation::PreviousMainIndex: command.stashNavigation = OnlineCnStashNavigation::PreviousMainIndex; break;
    case StorageNavigation::NextMainIndex: command.stashNavigation = OnlineCnStashNavigation::NextMainIndex; break;
    }
    return submitStorage(std::move(command));
}
bool RemoteUiClients::Impl::Inventory::selectStorageMaterials(bool open) {
    OnlineItemCommand command; command.action = OnlineItemAction::StorageMaterials; command.amount = open;
    return submitStorage(std::move(command));
}
bool RemoteUiClients::Impl::Inventory::withdrawStorageMaterial(std::string_view code, unsigned amount, bool cursor) {
    OnlineItemCommand command; command.action = OnlineItemAction::MaterialWithdraw;
    command.materialCode = code; command.amount = amount; command.toCursor = cursor;
    return submitStorage(std::move(command));
}
bool RemoteUiClients::Impl::Inventory::storeMaterialInCube(std::string_view code, unsigned amount) {
    OnlineItemCommand command; command.action = OnlineItemAction::MaterialWithdraw;
    command.materialCode = code; command.amount = amount; command.materialOperation = OnlineCnMaterialOperation::Cube;
    return submitStorage(std::move(command));
}
bool RemoteUiClients::Impl::Inventory::upgradeStorageMaterial(std::string_view code, unsigned amount) {
    OnlineItemCommand command; command.action = OnlineItemAction::MaterialWithdraw;
    command.materialCode = code; command.amount = amount; command.materialOperation = OnlineCnMaterialOperation::Upgrade;
    return submitStorage(std::move(command));
}
bool RemoteUiClients::Impl::Inventory::depositStorageMaterial(ItemHandle item) {
    OnlineItemCommand command; command.action = OnlineItemAction::MaterialDeposit;
    command.item = guid(item.id); command.itemRevision = item.revision;
    return submitStorage(std::move(command));
}

bool RemoteUiClients::Impl::Inventory::submitStaff(EntityId source,std::optional<ItemHandle> item) {
            if(source!=o.inventoryView.staffSource) return false;
            if(item) {const auto *value=o.inventoryView.item(item->id);if(!value || value->revision!=item->revision || value->definition!=o.data.staffRecipe.output) return false;}
            return o.session.submit_staff(uint32_t(source.value-1),item?std::optional{guid(item->id)}:std::nullopt,o.context);
        }
InventoryError RemoteUiClients::Impl::Inventory::preview(const InventoryIntent &intent) const {
            if (o.inventoryView.dead || o.inventoryView.readOnly) return InventoryError::AccessDenied;
            if (o.inventoryView.storagePagePending && !std::holds_alternative<CloseStorage>(intent))
                return InventoryError::AccessDenied;
            if (o.inventoryView.personalItemActionsOnly) {
                const auto &view = o.inventoryView;
                auto activeSlot = [&](EquipmentSlot slot) {
                    return size_t(slot) < size_t(EquipmentSlot::Count) && weaponSlotActive(slot, view.weaponSet);
                };
                auto personalContainer = [&](EntityId container) {
                    return container == view.containers.backpack || container == view.containers.cursor ||
                        container == view.containers.belt || container == view.containers.beltEquipment ||
                        container == view.containers.equipment ||
                        (container == view.containers.stash && o.session.read().world.storage.kind == OnlineStorageKind::Stash) ||
                        (container == view.containers.cube && o.session.read().world.storage.kind == OnlineStorageKind::Cube) ||
                        (container == view.ownTrade && o.session.read().world.playerTrade.phase == OnlinePlayerTrade::Phase::Open);
                };
                auto sourceAllowed = [&](ItemHandle handle) {
                    const auto *item = view.item(handle.id);
                    const auto *at = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
                    return at && personalContainer(at->container) &&
                        (at->container != view.containers.equipment ||
                            activeSlot(EquipmentSlot(at->cell.x)));
                };
                auto destinationAllowed = [&](const ItemDestination &destination) {
                    return std::visit([&](const auto &at) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(at)>, GroundLocation>) return true;
                        else {
                            if (!personalContainer(at.container)) return false;
                            if constexpr (std::is_same_v<std::decay_t<decltype(at)>, ContainerLocation>)
                                return at.container != view.containers.equipment ||
                                    activeSlot(EquipmentSlot(at.cell.x));
                            else return at.container == view.containers.backpack || at.container == view.containers.belt ||
                                at.container == view.containers.stash || at.container == view.containers.cube;
                        }
                    }, destination);
                };
                const bool allowed = std::visit([&](const auto &command) {
                    using T = std::decay_t<decltype(command)>;
                    if constexpr (std::is_same_v<T, MoveItem>)
                        return sourceAllowed(command.item) && destinationAllowed(command.destination);
                    else if constexpr (std::is_same_v<T, TransferItem>)
                        return sourceAllowed(command.item) && personalContainer(command.destination) &&
                            (command.destination == view.containers.backpack || command.destination == view.containers.belt ||
                                command.destination == view.containers.stash || command.destination == view.containers.cube);
                    else if constexpr (std::is_same_v<T, EquipItem>)
                        return sourceAllowed(command.item) && (!command.slot ||
                            activeSlot(*command.slot)) &&
                            (!command.destination || destinationAllowed(*command.destination));
                    else if constexpr (std::is_same_v<T, EquipBelt>)
                        return sourceAllowed(command.item) &&
                            (!command.destination || destinationAllowed(*command.destination));
                    else if constexpr (std::is_same_v<T, SwapItems>) {
                        const auto *target = view.item(command.second.id);
                        const auto *at = target ? std::get_if<ContainerLocation>(&target->location) : nullptr;
                        return sourceAllowed(command.first) && sourceAllowed(command.second) && at &&
                            (at->container == view.containers.backpack || at->container == view.containers.belt ||
                                at->container == view.containers.stash || at->container == view.containers.cube) &&
                            (!command.destination || command.destination->container == at->container);
                    }
                    else if constexpr (std::is_same_v<T, UseItem>) return sourceAllowed(command.item);
                    else if constexpr (std::is_same_v<T, MergeStacks>)
                        return sourceAllowed(command.source) && sourceAllowed(command.target);
                    else if constexpr (std::is_same_v<T, LoadBook>)
                        return sourceAllowed(command.scroll) && sourceAllowed(command.book);
                    else if constexpr (std::is_same_v<T, IdentifyItem>)
                        return sourceAllowed(command.source) && sourceAllowed(command.target);
                    else if constexpr (std::is_same_v<T, SocketItem>)
                        return sourceAllowed(command.filler) && sourceAllowed(command.host);
                    else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                        const auto *item = view.item(command.item.id);
                        const auto *at = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
                        return at && (sourceAllowed(command.item) || at->container == view.containers.hirelingEquipment);
                    }
                    else if constexpr (std::is_same_v<T, UseHirelingPotion>) return sourceAllowed(command.item);
                    else if constexpr (std::is_same_v<T, UseBeltColumn>) return command.column >= 0 && command.column < 4;
                    else if constexpr (std::is_same_v<T, TransmuteCube>) return o.session.read().world.storage.kind == OnlineStorageKind::Cube;
                    else if constexpr (std::is_same_v<T, GoldTransaction>)
                        return command.amount && (command.action == GoldAction::Drop ? view.goldKnown :
                            view.goldKnown && (command.action == GoldAction::Deposit || view.bankGoldKnown) && view.storageCnConfirmed && !view.storageShared &&
                            !view.storageMaterialsOpen && view.storagePagingAvailable && !view.storagePagePending);
                    else if constexpr (std::is_same_v<T, SwitchWeaponSet> || std::is_same_v<T, CloseStorage>) return true;
                    else return false;
                }, intent);
                if (!allowed) return InventoryError::AccessDenied;
            }
            return std::visit([&](const auto &c) {
                using T = std::decay_t<decltype(c)>;
                if (o.inventoryView.ownTrade) {
                    const auto &trade = o.session.read().world.playerTrade;
                    if (trade.ownAgreed || trade.response != OnlinePlayerTrade::Response::None)
                        return InventoryError::AccessDenied;
                    if constexpr (!std::is_same_v<T, MoveItem> && !std::is_same_v<T, TransferItem> && !std::is_same_v<T, SwapItems>)
                        return InventoryError::AccessDenied;
                    if constexpr (requires { c.item; }) {
                        const auto *item = o.inventoryView.item(c.item.id);
                        const auto *where = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
                        if (!where || (where->container != o.inventoryView.containers.backpack &&
                            where->container != o.inventoryView.ownTrade && where->container != o.inventoryView.containers.cursor))
                            return InventoryError::AccessDenied;
                    }
                    auto writable = [&](EntityId container) {
                        return container == o.inventoryView.ownTrade || container == o.inventoryView.containers.backpack ||
                            container == o.inventoryView.containers.cursor;
                    };
                    if constexpr (std::is_same_v<T, MoveItem>) {
                        const bool allowed = std::visit([&](const auto &destination) {
                            if constexpr (requires { destination.container; }) return writable(destination.container);
                            else return false;
                        }, c.destination);
                        if (!allowed) return InventoryError::AccessDenied;
                    } else if constexpr (std::is_same_v<T, TransferItem>) {
                        if (!writable(c.destination)) return InventoryError::AccessDenied;
                    } else if constexpr (std::is_same_v<T, SwapItems>) {
                        for (const auto handle : {c.first,c.second}) {
                            const auto *item = o.inventoryView.item(handle.id);
                            const auto *loc = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
                            if (!loc || !writable(loc->container)) return InventoryError::AccessDenied;
                            if (item->revision != handle.revision) return InventoryError::SourceChanged;
                        }
                    }
                }
                if constexpr (std::is_same_v<T, EquipItem> || std::is_same_v<T, EquipBelt>) {
                    const auto *item=o.inventoryView.item(c.item.id);
                    if(!item) return InventoryError::UnknownItem;
                    if(item->revision!=c.item.revision) return InventoryError::SourceChanged;
                    std::optional<EquipmentSlot> slot;
                    if constexpr (std::is_same_v<T, EquipItem>) slot = c.slot;
                    else {
                        // Preserve the original path; CN explicitly validates
                        // the belt item before showing a writable target.
                        if (!o.inventoryView.personalItemActionsOnly) return InventoryError::None;
                        const auto *at = std::get_if<ContainerLocation>(&item->location);
                        if (at && at->container == o.inventoryView.containers.beltEquipment) return InventoryError::None;
                        slot = EquipmentSlot::Belt;
                    }
                    if(!slot) return InventoryError::None;
                    const auto *definition=o.data.items.find(item->definition);
                    if(!definition) return InventoryError::UnknownDefinition;
                    if(!item->identified) return InventoryError::Unidentified;
                    if(!definition->equipment.known || !definition->equipment.fits(*slot))
                        return InventoryError::UnsupportedEquipment;
                    const auto &required=definition->equipment.requiredClass;
                    if(!required.empty() && required!=o.characterView.classCode) return InventoryError::WrongClass;
                    return InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                    const auto *item=o.inventoryView.item(c.item.id);
                    if(!o.hirelingView.active || !item || item->revision!=c.item.revision) return InventoryError::AccessDenied;
                    const auto *location=std::get_if<ContainerLocation>(&item->location);
                    const auto &containers=o.inventoryView.containers;
                    if(!location || (location->container!=containers.cursor && location->container!=containers.backpack &&
                        location->container!=containers.hirelingEquipment && location->container!=containers.equipment && location->container!=o.inventoryView.storage)) return InventoryError::AccessDenied;
                    if(c.slot) {
                        const auto *definition=o.data.items.find(item->definition);
                        const auto row=std::find_if(o.data.hirelings.begin(),o.data.hirelings.end(),[&](const auto &r){return r.classId==o.hirelingView.classId;});
                        if(!definition || row==o.data.hirelings.end()) return InventoryError::UnknownDefinition;
                        if(!item->identified) return InventoryError::Unidentified;
                        const auto &equipment=definition->equipment;
                        if((definition->maxDurability && !item->durability) || equipment.isType("ques")) return InventoryError::RestrictedItem;
                        const bool shield=row->act==3 && *c.slot==EquipmentSlot::LeftHand && equipment.isType("shld");
                        const bool extraSlot = o.inventoryView.personalItemActionsOnly &&
                            (*c.slot==EquipmentSlot::Belt || *c.slot==EquipmentSlot::Feet || *c.slot==EquipmentSlot::Gloves);
                        if(!equipment.fits(*c.slot) || (!shield && !extraSlot && *c.slot!=EquipmentSlot::Head && *c.slot!=EquipmentSlot::Torso && *c.slot!=EquipmentSlot::RightHand)) return InventoryError::UnsupportedEquipment;
                        if(*c.slot==EquipmentSlot::RightHand && ((!equipment.isType(row->weaponType1) && (row->weaponType2.empty() || !equipment.isType(row->weaponType2))) || (row->act==3 && equipment.twoHanded))) return InventoryError::UnsupportedEquipment;
                    }
                    return InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, MergeStacks>) {
                    const auto *target=o.inventoryView.item(c.target.id);
                    if (!target) return InventoryError::UnknownItem;
                    const auto *base=o.data.items.find(target->definition);
                    const auto native=o.session.read().world.items.find(guid(target->id));
                    const auto decoded=o.items.read().items.find(guid(target->id));
                    if (!base || native==o.session.read().world.items.end() || decoded==o.items.read().items.end())
                        return InventoryError::UnknownItem;
                    if (o.inventoryView.personalItemActionsOnly) {
                        const auto *source = o.inventoryView.item(c.source.id);
                        if (!source || source->id == target->id) return InventoryError::UnknownItem;
                        if (source->revision != c.source.revision || target->revision != c.target.revision) return InventoryError::SourceChanged;
                        const auto &world = o.session.read().world; const auto &items = o.items.read().items;
                        const auto sourceWire = world.items.find(guid(source->id));
                        const auto sourceData = items.find(guid(source->id));
                        if (sourceWire == world.items.end() || sourceData == items.end() ||
                            !onlinePd2StackCompatible(sourceWire->second, sourceData->second, native->second, decoded->second) ||
                            !base->maxStack || base->maxStack > 511) return InventoryError::IncompatibleStack;
                        return *decoded->second.quantity >= base->maxStack ? InventoryError::StackFull : InventoryError::None;
                    }
                    const auto instance=o.completeItem(native->second,decoded->second,target->location);
                    if (!instance) return InventoryError::InvalidRequest;
                    int64_t maximum=base->maxStack;
                    for (const auto &stat : resolveItemStats(o.data,*instance,o.characterView.level))
                        if (!stat.layer && (stat.effect.empty()?stat.name:stat.effect)=="item_extra_stack") maximum+=stat.value;
                    // A full target has no native mutation/ACK. Keep the cursor
                    // usable instead of waiting for a response that cannot arrive.
                    return maximum>0 && target->quantity>=uint64_t(maximum)
                        ? InventoryError::StackFull : InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, LoadBook>) {
                    const auto *source=o.inventoryView.item(c.scroll.id), *target=o.inventoryView.item(c.book.id);
                    if (!source || !target) return InventoryError::UnknownItem;
                    if (source->revision!=c.scroll.revision || target->revision!=c.book.revision) return InventoryError::SourceChanged;
                    const auto *definition=o.inventoryView.definition(target->definition);
                    // Original 0x29 transfers a scroll into a tome, never tome-to-tome.
                    if (source->id==target->id || !definition || !definition->bookCapacity ||
                        definition->bookScroll!=source->definition || source->quality!=ItemQuality::Normal || target->quality!=ItemQuality::Normal)
                        return InventoryError::IncompatibleStack;
                    return target->charges>=definition->bookCapacity ? InventoryError::StackFull : InventoryError::None;
                }
                else if constexpr (std::is_same_v<T, SplitStack>)
                    return InventoryError::InvalidRequest;
                else if constexpr (std::is_same_v<T, UseHirelingPotion>) {
                    const auto *item=o.inventoryView.item(c.item.id);
                    if (!item) return InventoryError::UnknownItem;
                    if (item->revision!=c.item.revision) return InventoryError::SourceChanged;
                    const auto *potion=o.data.potion(item->definition);
                    if (!potion || (potion->kind!=PotionKind::Healing && potion->kind!=PotionKind::Rejuvenation && potion->kind!=PotionKind::Remedy))
                        return InventoryError::UnsupportedUse;
                    const auto *where=std::get_if<ContainerLocation>(&item->location);
                    if (!o.hirelingView.active && (!where || where->container!=o.inventoryView.containers.belt))
                        return InventoryError::AccessDenied;
                    if (!where || (where->container!=o.inventoryView.containers.backpack &&
                        where->container!=o.inventoryView.containers.cursor && where->container!=o.inventoryView.containers.belt))
                        return InventoryError::AccessDenied;
                    return InventoryError::None;
                }
                else if constexpr (requires { c.item; }) {
                    const auto *item = o.inventoryView.item(c.item.id);
                    return !item ? InventoryError::UnknownItem : item->revision != c.item.revision
                        ? InventoryError::SourceChanged : InventoryError::None;
                } else return InventoryError::None;
            }, intent);
        }
void RemoteUiClients::Impl::Inventory::submit(InventoryIntent c) { o.inventoryCommand(std::move(c)); }
std::optional<Cell> RemoteUiClients::Impl::Inventory::beltSpace(std::string_view code) const {
            const auto belt = o.inventoryView.containers.belt;
            const auto *grid = o.inventoryView.container(belt);
            const auto *base=o.inventoryView.definition(code);
            if (!grid || !base || !base->beltAllowed) return {};
            for (int x = 0; x < grid->columns; ++x) {
                const auto *bottom=o.inventoryView.item(o.inventoryView.itemAt(belt,{x,0}));
                const auto *other=bottom?o.inventoryView.definition(bottom->definition):nullptr;
                const bool compatible=other && (other->code==code || (!base->beltPotionType.empty() && other->beltPotionType==base->beltPotionType));
                if (!compatible) continue;
                for (int y = 0; y < grid->rows; ++y)
                    if (!o.inventoryView.itemAt(belt, {x,y})) return Cell{x,y};
            }
            if(base->autoBelt) for(int x=0;x<grid->columns;++x)
                if(!o.inventoryView.itemAt(belt,{x,0})) return Cell{x,0};
            return {};
        }
std::optional<Cell> RemoteUiClients::Impl::freeCell(EntityId target, const InventoryItemView &item) const {
        const auto *grid = inventoryView.container(target);
        const auto *def = inventoryView.definition(item.definition);
        if (!grid || !def) return {};
        for (int y=0;y+def->height<=grid->rows;++y) for(int x=0;x+def->width<=grid->columns;++x) {
            bool free = true;
            for(int yy=y;yy<y+def->height;++yy) for(int xx=x;xx<x+def->width;++xx) {
                const auto occupant = inventoryView.itemAt(target,{xx,yy});
                free &= !occupant || occupant == item.id;
            }
            if (free) return Cell{x,y};
        }
        return {};
    }
void RemoteUiClients::Impl::moveItem(ItemHandle handle, ItemDestination destination) {
        const auto *item = inventoryView.item(handle.id);
        if (!item || item->revision != handle.revision) { notice = "Item changed; select it again."; return; }
        OnlineItemCommand next; next.item = guid(handle.id);
        const auto &owned = inventoryView.containers;
        if (std::holds_alternative<GroundLocation>(destination)) next.action = OnlineItemAction::Drop;
        else {
            ContainerLocation target;
            if (const auto *automatic = std::get_if<AutoPlace>(&destination)) {
                if (automatic->container == owned.cube) {
                    for (const auto &[id, cube] : inventoryView.items) {
                        const auto *base = inventoryView.definition(cube.definition);
                        const auto *at = std::get_if<ContainerLocation>(&cube.location);
                        if (!base || !base->opensCube || !at ||
                            (at->container != owned.backpack && at->container != inventoryView.storage)) continue;
                        next.action = OnlineItemAction::CubeStore; next.target = guid(id);
                        next.itemRevision = handle.revision; next.targetRevision = cube.revision;
                        if (items.read().cursor != next.item) compositeTake(next);
                        else enqueue(next);
                        return;
                    }
                    notice = "An accessible stored cube is required."; return;
                }
                auto cell = freeCell(automatic->container,*item);
                if (!cell) { notice = "No room in that container."; return; }
                target = {automatic->container,*cell};
            } else target = std::get<ContainerLocation>(destination);
            if (target.container == owned.cursor) {
                const auto *origin=std::get_if<ContainerLocation>(&item->location);
                next.action = origin && origin->container==owned.hirelingEquipment?OnlineItemAction::HirelingEquipment:OnlineItemAction::Take;
                if(next.action==OnlineItemAction::HirelingEquipment) next.body=nativeBody(EquipmentSlot(origin->cell.x));
                next.itemRevision = handle.revision; enqueue(next); return;
            }
            next.x = uint8_t(target.cell.x); next.y = uint8_t(target.cell.y);
            if (target.container == owned.belt) {
                const auto *origin=std::get_if<ContainerLocation>(&item->location);
                if(std::holds_alternative<AutoPlace>(destination) && origin && origin->container==owned.backpack && !items.read().cursor) {
                    next.action=OnlineItemAction::BeltAuto;next.itemRevision=handle.revision;enqueue(next);return;
                }
                next.action = OnlineItemAction::BeltPlace; next.beltSlot = uint8_t(target.cell.y*4+target.cell.x);
            } else if(target.container==owned.hirelingEquipment) {
                next.action=OnlineItemAction::HirelingEquipment;next.body=nativeBody(EquipmentSlot(target.cell.x));
            } else if (target.container == owned.equipment || target.container == owned.beltEquipment) {
                next.action = OnlineItemAction::Equip; next.body = target.container == owned.beltEquipment ? 8 : nativeBody(EquipmentSlot(target.cell.x));
            } else {
                next.action = OnlineItemAction::Place;
                if (target.container == inventoryView.peerTrade && inventoryView.peerTrade) {
                    notice = "The other player's offer is read-only."; return;
                }
                next.page = target.container == inventoryView.ownTrade && inventoryView.ownTrade ? 2 : target.container == owned.cube ? 3 : target.container == owned.stash ? 4 : 0;
            }
        }
        if (waiting || !transactions.empty()) { notice = "Waiting for the previous server item response."; return; }
        if (items.read().cursor != next.item) {
            OnlineItemCommand take; take.action = OnlineItemAction::Take; take.item = next.item; take.itemRevision = handle.revision;
            if(const auto *origin=std::get_if<ContainerLocation>(&item->location);origin && origin->container==owned.hirelingEquipment) {
                take.action=OnlineItemAction::HirelingEquipment;take.body=nativeBody(EquipmentSlot(origin->cell.x));
            }
            queue(std::move(take));
        } else next.itemRevision = handle.revision;
        queue(std::move(next)); pump();
    }
void RemoteUiClients::Impl::compositeTake(OnlineItemCommand next) {
        if (waiting || !transactions.empty()) { notice="Waiting for the previous server item response."; return; }
        OnlineItemCommand take; take.action=OnlineItemAction::Take; take.item=next.item; take.itemRevision=next.itemRevision;
        next.itemRevision=0;
        queue(std::move(take)); queue(std::move(next)); pump();
    }
void RemoteUiClients::Impl::inventoryCommand(InventoryIntent intent) {
        if (inventoryView.readOnly) { notice = "Item interactions are unavailable for this client profile."; return; }
        if (inventoryView.personalItemActionsOnly && inventory.preview(intent) != InventoryError::None) {
            notice = "Only personal inventory item movement is available for this client profile."; return;
        }
        std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            const auto &owned = inventoryView.containers;
            OnlineItemCommand c;
            if constexpr (std::is_same_v<T, CloseStorage>) {
                if (!onlineWorldMatches(context, session.read())) { notice = "Storage belongs to a previous world."; return; }
                // Explicit close cancels unsent combinations. It has no native ACK
                // and must not leave a coordinator wait in front of a new cube open.
                transactions.clear(); waiting.reset(); waitingContext.reset();
                c.action = OnlineItemAction::StorageClose; c.context = onlineIntentContext(session.read());
                if (!items.submit(session, c)) notice = items.reason();
                context = onlineIntentContext(session.read());
                return;
            } else if constexpr (std::is_same_v<T, MoveItem>) { moveItem(v.item,v.destination); return; }
            else if constexpr (std::is_same_v<T, TransferItem>) { moveItem(v.item,AutoPlace{v.destination}); return; }
            else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                if(v.slot) moveItem(v.item,ContainerLocation{owned.hirelingEquipment,{int(*v.slot),0}});
                else moveItem(v.item,v.destination.value_or(ItemDestination{ContainerLocation{owned.cursor,{}}}));
                return;
            }
            else if constexpr (std::is_same_v<T, EquipItem> || std::is_same_v<T, EquipBelt>) {
                const auto *item = inventoryView.item(v.item.id); if (!item) return;
                const auto *location = std::get_if<ContainerLocation>(&item->location);
                if (location && (location->container == owned.equipment || location->container == owned.beltEquipment)) {
                    moveItem(v.item,v.destination.value_or(ItemDestination{AutoPlace{owned.backpack}})); return;
                }
                EquipmentSlot slot = EquipmentSlot::Belt;
                if constexpr (std::is_same_v<T, EquipItem>) { if (!v.slot) return; slot = *v.slot; }
                moveItem(v.item,ContainerLocation{owned.equipment,{int(slot),0}}); return;
            } else if constexpr (std::is_same_v<T, UseItem> || std::is_same_v<T, UseHirelingPotion>) {
                if constexpr (std::is_same_v<T, UseItem>) {
                    const auto *item = inventoryView.item(v.item.id);
                    if (item && item->revision == v.item.revision) {
                        const auto *definition = data.items.find(item->definition);
                        if (definition && definition->opensCube) {
                            if (!onlineWorldMatches(context, session.read())) { notice = "Cube belongs to a previous world."; return; }
                            c.action = OnlineItemAction::CubeOpen;
                            c.item = guid(v.item.id); c.itemRevision = v.item.revision;
                            c.context = onlineIntentContext(session.read());
                            enqueue(c); return;
                        }
                    }
                }
                c.action = OnlineItemAction::Use; c.item = guid(v.item.id); c.itemRevision = v.item.revision;
                if constexpr (std::is_same_v<T, UseHirelingPotion>) {
                    if (inventory.preview(v)!=InventoryError::None) {notice="Item cannot be used on this hireling.";return;}
                    c.mercenary=true;
                    const auto native=session.read().world.items.find(c.item);
                    if(native==session.read().world.items.end()) return;
                    if(native->second.mode!=2) {
                        c.action=OnlineItemAction::HirelingEquipment;c.body=0;
                        if(items.read().cursor!=c.item) {compositeTake(c);return;}
                    }
                }
            } else if constexpr (std::is_same_v<T, UseBeltColumn>) {
                if (const auto *item = inventoryView.item(inventoryView.itemAt(owned.belt,{v.column,0}))) {
                    if (v.hireling && inventory.preview(UseHirelingPotion{item->handle()})!=InventoryError::None) return;
                    c.action = OnlineItemAction::Use; c.item = guid(item->id); c.itemRevision = item->revision; c.mercenary = v.hireling;
                }
                if (c.action != OnlineItemAction::Use) return;
            } else if constexpr (std::is_same_v<T, SwapItems>) {
                c.item = guid(v.first.id); c.itemRevision = v.first.revision; c.target = guid(v.second.id); c.targetRevision = v.second.revision;
                const auto *target = inventoryView.item(v.second.id);
                const auto *loc = target ? std::get_if<ContainerLocation>(&target->location) : nullptr;
                c.action = loc && loc->container == owned.belt ? OnlineItemAction::BeltSwap : OnlineItemAction::Swap;
                if (loc) c.beltSlot = uint8_t(loc->cell.y*4+loc->cell.x);
                if (v.destination && (!loc || v.destination->container != loc->container)) {
                    notice = "Swap destination must belong to the covered item's container."; return;
                }
                if (loc) {
                    const auto cell = v.destination ? v.destination->cell : loc->cell;
                    if (cell.x < 0 || cell.y < 0 || cell.x > UINT8_MAX || cell.y > UINT8_MAX) {
                        notice = "Swap destination is outside the native grid coordinate range."; return;
                    }
                    c.x = uint8_t(cell.x); c.y = uint8_t(cell.y);
                }
                if (items.read().cursor != c.item) { compositeTake(c); return; }
            } else if constexpr (std::is_same_v<T, MergeStacks> || std::is_same_v<T, LoadBook> || std::is_same_v<T, SocketItem> || std::is_same_v<T, IdentifyItem>) {
                if constexpr (std::is_same_v<T, MergeStacks>) { c.action=OnlineItemAction::Stack; c.item=guid(v.source.id); c.itemRevision=v.source.revision; c.target=guid(v.target.id); c.targetRevision=v.target.revision; }
                if constexpr (std::is_same_v<T, LoadBook>) { c.action=OnlineItemAction::Book; c.item=guid(v.scroll.id); c.itemRevision=v.scroll.revision; c.target=guid(v.book.id); c.targetRevision=v.book.revision; }
                if constexpr (std::is_same_v<T, SocketItem>) { c.action=OnlineItemAction::Socket; c.item=guid(v.filler.id); c.itemRevision=v.filler.revision; c.target=guid(v.host.id); c.targetRevision=v.host.revision; }
                if constexpr (std::is_same_v<T, IdentifyItem>) { c.action=OnlineItemAction::Identify; c.item=guid(v.source.id); c.itemRevision=v.source.revision; c.target=guid(v.target.id); c.targetRevision=v.target.revision; }
            } else if constexpr (std::is_same_v<T, SwitchWeaponSet>) c.action = OnlineItemAction::SwitchWeapons;
            else if constexpr (std::is_same_v<T, TransmuteCube>) c.action = OnlineItemAction::Transmute;
            else if constexpr (std::is_same_v<T, GoldTransaction>) {
                c.action = v.action == GoldAction::Deposit ? OnlineItemAction::GoldDeposit : v.action == GoldAction::Withdraw ? OnlineItemAction::GoldWithdraw : OnlineItemAction::GoldDrop; c.amount = v.amount;
            } else { notice = "This native item operation is unavailable."; return; }
            if ((c.action == OnlineItemAction::Socket || c.action == OnlineItemAction::Book) && items.read().cursor != c.item) { compositeTake(c); return; }
            enqueue(c);
        }, intent);
    }
}

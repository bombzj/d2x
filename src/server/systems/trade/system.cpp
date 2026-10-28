#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/inventory/eligibility.hpp"
#include "server/systems/npc/system.hpp"
#include "gameplay/items/inventory_order.hpp"
#include <algorithm>

namespace d2x::server::trade {
namespace {
constexpr uint32_t duplicatedSourceFlag=0x08000000; // ITEMS_Duplicate marks its source, not its copy.
DomainResult<> applied() { return {DomainStatus::Applied,std::monostate{}}; }
const Offer &offer(const Exchange &e,PlayerId id) { return e.first.player==id?e.first:e.second; }
const Offer &other(const Exchange &e,PlayerId id) { return e.first.player==id?e.second:e.first; }
bool cursorEmpty(const PlayerState &p) {
    return std::none_of(p.persistent.inventory.items.begin(),p.persistent.inventory.items.end(),[&](const auto &entry){
        const auto *at=std::get_if<ContainerLocation>(&entry.second.location);
        return at && at->container==p.persistent.containers.cursor;
    });
}
bool personalItem(const InventoryState &inventory,const ItemInstance &item,EntityId owner,EntityId hirelingEquipment) {
    const auto *at=std::get_if<ContainerLocation>(&item.location);
    if(!at || at->container==hirelingEquipment) return false;
    const auto &container=inventory.containers.at(at->container);
    return container.spec.owner==owner && container.spec.kind!=ContainerKind::Corpse;
}
ActorContext context(const PlayerState &p,const AreaStore &areas,uint64_t tick) {
    return {p.player,p.actor,p.area,areas.at(p.area).generation,0,tick};
}
std::vector<EntityId> offered(const InventoryState &inventory,EntityId container) {
    std::vector<EntityId> result;
    for (const auto id : inventoryItemOrder(inventory)) {
        const auto &item=inventory.items.at(id);
        const auto *at=std::get_if<ContainerLocation>(&item.location);
        if (at && at->container==container) result.push_back(id);
    }
    return result;
}
size_t identityCount(const ItemInstance &item) {
    size_t count=1;
    for(const auto &child:item.socketedItems) count+=identityCount(child);
    return count;
}
ItemInstance copyItem(ItemInstance item,const std::map<EntityId,EntityId> &identities) {
    item.id=identities.at(item.id);
    item.revision=1;
    for(auto &child:item.socketedItems) {
        child=copyItem(std::move(child),identities);
        std::get<SocketLocation>(child.location).host=item.id;
    }
    return item;
}
void reserveCopy(const ItemInstance &item,items::System &items,std::map<EntityId,EntityId> &identities) {
    identities.emplace(item.id,items.reserveIdentity());
    for(const auto &child:item.socketedItems) reserveCopy(child,items,identities);
}
void reserveMirror(const ItemInstance &item,items::System &items,Mirrors &identities) {
    identities.emplace(item.id,Mirror{items.reserveIdentity(),item.nativeFlags});
    for(const auto &child:item.socketedItems) reserveMirror(child,items,identities);
}
void markCopied(ItemInstance &item) {
    item.nativeFlags |= duplicatedSourceFlag;
    for(auto &child:item.socketedItems) markCopied(child);
}
ItemInstance mirrorItem(ItemInstance item,const Mirrors &identities) {
    const auto &mirror=identities.at(item.id);
    item.id=mirror.id; item.nativeFlags=mirror.flags;
    for(auto &child:item.socketedItems) {
        child=mirrorItem(std::move(child),identities);
        std::get<SocketLocation>(child.location).host=item.id;
    }
    return item;
}
TradeItemsFact mirrors(const PlayerState &p,const Offer &own,const transactions::InventoryEdit &change) {
    const auto &next=change.inventory;
    const auto changed=[&](EntityId id) {
        return std::any_of(change.changes.begin(),change.changes.end(),[&](const auto &delta){return delta.item==id;});
    };
    TradeItemsFact fact; fact.projection.player=p.persistent.player;
    fact.projection.containers=p.persistent.containers;
    fact.projection.inventory.containers=next.containers;
    for (auto id : offered(p.persistent.inventory,own.container))
        if(changed(id)) fact.removed.push_back(mirrorItem(p.persistent.inventory.items.at(id),own.mirrors));
    for (auto id : offered(next,own.container)) {
        if(!changed(id)) continue;
        auto item=mirrorItem(next.items.at(id),own.mirrors);
        fact.projection.inventory.items.emplace(item.id,std::move(item));
    }
    return fact;
}
TradeItemsFact clearMirrors(const PlayerState &p,const Offer &own) {
    TradeItemsFact fact; fact.projection.player=p.persistent.player;
    fact.completed=true;
    fact.projection.containers=p.persistent.containers;
    fact.projection.inventory.containers=p.persistent.inventory.containers;
    for(auto id:offered(p.persistent.inventory,own.container)) fact.removed.push_back(mirrorItem(p.persistent.inventory.items.at(id),own.mirrors));
    return fact;
}
transactions::InventoryEdit edit(const PlayerState &p,const AreaStore &areas,uint64_t tick,InventoryState inventory) {
    transactions::InventoryEdit result{context(p,areas,tick),p.inventoryRevision,p.characterRevision,
        std::move(inventory),{},p.persistent.player.weaponSet};
    result.character=p.persistent.player;
    result.playerTrade=true;
    return result;
}
std::vector<EventBatch> notifications(const PlayerStore &players,const Exchange &e,uint64_t tick,uint8_t action) {
    std::vector<EventBatch> result;
    for (const auto *own : {&e.first,&e.second}) {
        const auto &p=*players.find(own->player);
        result.push_back({0,tick,{}, {AudienceKind::Player,own->player,p.area},
            {TradeFact{action}}});
    }
    return result;
}
}
const Exchange *System::find(PlayerId id) const {
    for (const auto &[key,e] : state_.exchanges) { (void)key; if(e.first.player==id || e.second.player==id) return &e; }
    return nullptr;
}
EntityId System::container(PlayerId id) const {
    const auto *e=find(id); return e && e->open?offer(*e,id).container:EntityId{};
}
bool System::canEdit(PlayerId id) const {
    const auto *e=find(id); return e && e->open && !offer(*e,id).agreed;
}
void System::normalize(PlayerId id,PersistentCharacter &save) const {
    if (const auto *e=find(id); e && e->open) {
        const auto &own=offer(*e,id);
        std::erase_if(save.inventory.items,[&](const auto &entry){return personalItem(save.inventory,entry.second,save.player.id,save.containers.hirelingEquipment);});
        save.inventory.itemOrder=inventoryItemOrder(save.inventory);
        for(const auto key:inventoryItemOrder(own.original)) {
            const auto &item=own.original.items.at(key);
            if(!personalItem(own.original,item,save.player.id,save.containers.hirelingEquipment)) continue;
            save.inventory.items.emplace(key,item);save.inventory.itemOrder.push_back(key);
        }
        save.inventory.containers.erase(own.container);
        save.inventory.itemOrder=inventoryItemOrder(save.inventory);
    }
}
DomainResult<> System::decorate(PlayerId id,transactions::InventoryEdit &change,bool removedOffer,Mirrors &identities) {
    const auto *e=find(id); if (!e) return applied();
    if (!canEdit(id) || e->revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    auto own=offer(*e,id); const auto &peer=other(*e,id);
    const auto &p=*ports_.players.find(id);
    size_t count=0;
    for(auto item:offered(change.inventory,own.container))
        if(!own.mirrors.contains(item)) count+=identityCount(change.inventory.items.at(item));
    if(!ports_.items.identityCapacity(count)) return {DomainStatus::Capacity,{}};
    for(auto item:offered(change.inventory,own.container))
        if(!own.mirrors.contains(item)) reserveMirror(change.inventory.items.at(item),ports_.items,own.mirrors);
    for(auto item:offered(change.inventory,own.container))
        markCopied(change.inventory.items.at(item));
    auto projection=mirrors(p,own,change);
    identities=own.mirrors;
    const auto erase=[&](auto &&self,const ItemInstance &item)->void {
        identities.erase(item.id);for(const auto &child:item.socketedItems) self(self,child);
    };
    const auto next=offered(change.inventory,own.container);
    for(auto item:offered(p.persistent.inventory,own.container)) {
        if(std::find(next.begin(),next.end(),item)==next.end()) erase(erase,p.persistent.inventory.items.at(item));
    }
    // PlrTrade::sub_6FC931D0 locks removal; sub_6FC92F10 does not lock additions.
    // Fixed 1.13c sends the agreement reset before the lock and retains gold.
    if(own.agreed || peer.agreed) {
        change.facts.emplace_back(TradeFact{6});
        change.directed.push_back({peer.player,{TradeFact{6}}});
    }
    if(removedOffer) {
        change.facts.emplace_back(TradeFact{14});
        change.directed.push_back({peer.player,{TradeFact{14}}});
    }
    change.directed.push_back({peer.player,{std::move(projection)}});
    return applied();
}
void System::edited(PlayerId id,uint64_t tick,bool removedOffer,Mirrors identities) noexcept {
    for (auto &[key,e] : state_.exchanges) if (e.first.player==id || e.second.player==id) {
        (void)key; e.first.agreed=e.second.agreed=false; ++e.revision;
        (e.first.player==id?e.first:e.second).mirrors.swap(identities);
        if(removedOffer) e.unlock=tick+250;
        return;
    }
}
void System::refreshItem(PlayerId id,transactions::InventoryEdit &change) const {
    const auto *exchange=find(id);if(!exchange || !exchange->open) return;
    const auto &own=offer(*exchange,id);
    const auto &player=*ports_.players.find(id);
    auto projection=mirrors(player,own,change);
    if(!projection.removed.empty() || !projection.projection.inventory.items.empty())
        change.directed.push_back({other(*exchange,id).player,{std::move(projection)}});
    change.playerTrade=true;
}
DomainResult<> System::execute(const ActorContext &actor,const Request &request) {
    const auto *p=ports_.players.find(actor.player); const auto *area=ports_.areas.find(actor.area);
    if (!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !area || area->generation!=actor.areaGeneration)
        return {DomainStatus::InvalidActor,{}};
    const auto *existing=find(actor.player);
    if (request.action==Action::Cancel) return cancelFor(actor.player,actor.tick);
    if (request.action==Action::Invite) {
        const auto *peer=request.peer?ports_.players.find(*request.peer):nullptr;
        if (!peer || peer==p || !peer->entered || peer->area!=p->area || !area->definition.town ||
            p->persistent.player.hp<=0 || peer->persistent.player.hp<=0 || (p->position-peer->position).length()>50) return {DomainStatus::InvalidRequest,{}};
        if (existing || find(peer->player) || ports_.npc.conversation(p->player) || ports_.npc.conversation(peer->player) ||
            !cursorEmpty(*p) || !cursorEmpty(*peer) || ports_.inventory.storageAccess(p->player) || ports_.inventory.storageAccess(peer->player) ||
            ports_.inventory.cubeAccess(p->player) || ports_.inventory.cubeAccess(peer->player) ||
            (state_.cooldown.contains(p->player) && actor.tick<state_.cooldown.at(p->player)) ||
            (state_.cooldown.contains(peer->player) && actor.tick<state_.cooldown.at(peer->player))) return {DomainStatus::Conflict,{}};
        if (state_.next==UINT64_MAX) return {DomainStatus::Capacity,{}};
        Exchange next; next.transaction={state_.next}; next.first.player=p->player; next.second.player=peer->player;
        auto exchanges=state_.exchanges; exchanges.emplace(next.transaction,next);
        auto published=ports_.events.publishGroup({
            {0,actor.tick,{}, {AudienceKind::Player,p->player,p->area},{TradeFact{0}}},
            {0,actor.tick,{}, {AudienceKind::Player,peer->player,peer->area},{TradeFact{1}}}});
        if (!published) return {published.status,{}};
        state_.exchanges.swap(exchanges); ++state_.next;
        ports_.inventory.close(p->player); ports_.inventory.close(peer->player); return applied();
    }
    if (!existing || (request.revision && request.revision!=existing->revision)) return {DomainStatus::Stale,{}};
    auto next=*existing; const auto key=next.transaction;
    auto &own=next.first.player==p->player?next.first:next.second;
    auto &peerOffer=next.first.player==p->player?next.second:next.first;
    const auto *peer=ports_.players.find(peerOffer.player);
    if (!peer || !peer->entered || peer->area!=p->area || p->persistent.player.hp<=0 || peer->persistent.player.hp<=0 || !area->definition.town)
        return cancelFor(actor.player,actor.tick);
    if (request.action==Action::Accept) {
        if (next.open || p->player!=next.second.player || !cursorEmpty(*p) || !cursorEmpty(*peer)) return {DomainStatus::Conflict,{}};
        if (!ports_.items.identityCapacity(2)) return {DomainStatus::Capacity,{}};
        std::vector<transactions::Change> changesToPrepare;
        for (auto *o : {&next.first,&next.second}) {
            const auto &player=*ports_.players.find(o->player);
            if (!player.rules.character || !player.rules.equipment) return {DomainStatus::Unavailable,{}};
            o->container=ports_.items.reserveIdentity(std::nullopt); o->original=player.persistent.inventory; o->equipment=player.rules.equipment;
            auto inventory=o->original;
            inventory.containers.emplace(o->container,ContainerState{o->container,{player.actor,ContainerKind::Trade,
                player.rules.character->tradeColumns,player.rules.character->tradeRows}});
            auto change=edit(player,ports_.areas,actor.tick,std::move(inventory));
            const auto &partner=*ports_.players.find(other(next,o->player).player);
            change.facts.emplace_back(TradeFact{6,partner.actor,partner.persistent.player.name});
            changesToPrepare.emplace_back(std::move(change));
        }
        auto prepared=ports_.transactions.prepareGroup(std::move(changesToPrepare));if(!prepared) return {prepared.status,{}};
        const auto result=ports_.transactions.commitInventories(std::move(*prepared.value)); if(!result) return result;
        next.open=true; ++next.revision; state_.exchanges.at(key)=std::move(next); return applied();
    }
    if (!next.open || next.revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    if (request.action==Action::OfferGold) {
        if (!cursorEmpty(*p) || request.gold>p->persistent.player.gold) return {DomainStatus::InvalidRequest,{}};
        const auto limit=uint64_t(peer->persistent.player.level)*10000;
        const auto amount=unsigned(std::min<uint64_t>(request.gold,limit>peer->persistent.player.gold?limit-peer->persistent.player.gold:0));
        // Changes after agreement must first revoke it via 4F/7. A duplicate
        // quote only acknowledges gold; it must not clear either checkbox.
        if(amount!=own.gold && (own.agreed || peerOffer.agreed)) return {DomainStatus::Conflict,{}};
        own.gold=amount; ++next.revision;
        const auto published=ports_.events.publishGroup({
            {0,actor.tick,{}, {AudienceKind::Player,p->player,p->area},{TradeFact{{},{},{},amount,{}}}},
            {0,actor.tick,{}, {AudienceKind::Player,peer->player,peer->area},{TradeFact{{},{},{},{},amount}}}});
        if(!published) return {published.status,{}};
        state_.exchanges.at(key)=std::move(next); return applied();
    }
    if (request.action==Action::Revoke) {
        next.first.agreed=next.second.agreed=false; ++next.revision;
        auto published=ports_.events.publishGroup(notifications(ports_.players,next,actor.tick,6));
        if (!published) return {published.status,{}};
        state_.exchanges.at(key)=std::move(next); return applied();
    }
    if (request.action!=Action::Agree || !cursorEmpty(*p) || actor.tick<next.unlock || own.agreed) return {DomainStatus::Conflict,{}};
    own.agreed=true; ++next.revision;
    if (peerOffer.agreed) return complete(next,actor.tick);
    auto published=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,peer->player,peer->area},{TradeFact{5}}});
    if(!published) return {published.status,{}};
    state_.exchanges.at(key)=std::move(next); return applied();
}
DomainResult<> System::complete(Exchange &e,uint64_t tick) {
    const auto &first=*ports_.players.find(e.first.player), &second=*ports_.players.find(e.second.player);
    if (!cursorEmpty(first) || !cursorEmpty(second)) return {DomainStatus::Conflict,{}};
    std::array<InventoryState,2> inventories{first.persistent.inventory,second.persistent.inventory};
    std::array<std::vector<ItemChange>,2> changes;
    std::array<std::shared_ptr<EquipmentRules>,2> rules{
        std::make_shared<EquipmentRules>(*first.rules.equipment),std::make_shared<EquipmentRules>(*second.rules.equipment)};
    const std::array<const PlayerState *,2> players{&first,&second};
    const std::array<const Offer *,2> offers{&e.first,&e.second};
    std::array<std::vector<ItemInstance>,2> incoming;
    size_t identities=0;
    for(size_t side=0;side<2;++side) for(auto id:offered(inventories[side],offers[side]->container))
        identities+=identityCount(inventories[side].items.at(id));
    if(!ports_.items.identityCapacity(identities)) return {DomainStatus::Capacity,{}};
    for(size_t side=0;side<2;++side) for(auto id:offered(inventories[side],offers[side]->container)) {
        const auto &item=inventories[side].items.at(id);
        if(item.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        std::map<EntityId,EntityId> copies;
        reserveCopy(item,ports_.items,copies);
        auto received=copyItem(item,copies);
        incoming[1-side].push_back(std::move(received));
        changes[side].push_back({id,item.revision+1,ItemChangeKind::Removed,item.location,{},0});
        inventories[side].items.erase(id);
        rules[1-side]->items.emplace(copies.at(id),rules[side]->items.at(id)); rules[1-side]->includeSets(*rules[side]);
    }
    DomainStatus failure=DomainStatus::Applied; size_t failedSide=0;
    for (size_t side=0;side<2 && failure==DomainStatus::Applied;++side) {
        auto receiver=*players[side]; receiver.persistent.inventory=inventories[side];
        inventory::detail::Draft draft(receiver,*receiver.rules.items,*rules[side],*receiver.rules.character);
        for(auto item:incoming[side]) {
            const auto *base=receiver.rules.items->find(item.definition);
            if(!base) {failure=DomainStatus::Unavailable;break;}
            receiver.persistent.inventory=draft.edit.inventory;
            failure=inventory::canReceiveQuestItem(receiver,*base,*receiver.rules.items,unsigned(receiver.persistent.difficulty));
            if (failure!=DomainStatus::Applied) break;
            if(rules[side]->items.at(item.id).singleCarry && std::any_of(draft.edit.inventory.items.begin(),draft.edit.inventory.items.end(),[&](const auto &entry){
                const auto *at=std::get_if<ContainerLocation>(&entry.second.location);
                return at && draft.edit.inventory.containers.at(at->container).spec.kind!=ContainerKind::Stash &&
                    entry.second.specialRow==item.specialRow && entry.second.quality==ItemQuality::Unique;
            })) {failure=DomainStatus::Conflict;break;}
            const auto id=item.id;
            draft.edit.inventory.items.emplace(id,std::move(item));
            const auto target=draft.space(id,receiver.persistent.containers.backpack);
            if(!target) {failure=DomainStatus::Conflict;break;}
            auto &placed=draft.edit.inventory.items.at(id); placed.location=*target;
            changes[side].push_back({id,placed.revision,ItemChangeKind::Created,{},placed.location,placed.quantity});
        }
        inventories[side]=std::move(draft.edit.inventory); failedSide=side;
    }
    for(size_t side=0;side<2;++side) {
        const uint64_t gold=uint64_t(players[side]->persistent.player.gold);
        if (offers[side]->gold>gold || gold-offers[side]->gold+offers[1-side]->gold>uint64_t(players[side]->persistent.player.level)*10000) {
            failure=DomainStatus::Conflict; failedSide=side;
        }
    }
    if(failure!=DomainStatus::Applied) {
        e.first.agreed=e.second.agreed=false;
        auto batches=notifications(ports_.players,e,tick,6);
        batches[failedSide].facts.emplace_back(TradeFact{9}); batches[1-failedSide].facts.emplace_back(TradeFact{10});
        const auto sent=ports_.events.publishGroup(std::move(batches));
        if(!sent) return {sent.status,{}};
        state_.exchanges.at(e.transaction)=std::move(e); return applied();
    }
    auto cooldown=state_.cooldown; cooldown[e.first.player]=tick+125; cooldown[e.second.player]=tick+125;
    std::vector<transactions::Change> changesToPrepare;
    for(size_t side=0;side<2;++side) {
        inventories[side].containers.erase(offers[side]->container);
        auto change=edit(*players[side],ports_.areas,tick,std::move(inventories[side]));
        change.changes=std::move(changes[side]); change.equipment=rules[side];
        change.character->gold=players[side]->persistent.player.gold-offers[side]->gold+offers[1-side]->gold;
        change.facts.emplace_back(clearMirrors(*players[1-side],*offers[1-side]));
        change.facts.emplace_back(TradeFact{13});
        changesToPrepare.emplace_back(std::move(change));
    }
    auto prepared=ports_.transactions.prepareGroup(std::move(changesToPrepare));if(!prepared) return {prepared.status,{}};
        const auto result=ports_.transactions.commitInventories(std::move(*prepared.value)); if(!result) return result;
    state_.cooldown.swap(cooldown); state_.exchanges.erase(e.transaction); return applied();
}
DomainResult<> System::cancelFor(PlayerId id,uint64_t tick) {
    const auto *existing=find(id); if(!existing) return applied();
    const auto e=*existing;
    auto cooldown=state_.cooldown; cooldown[e.first.player]=tick+125; cooldown[e.second.player]=tick+125;
    if (!e.open) {
        const auto result=ports_.events.publishGroup(notifications(ports_.players,e,tick,12));
        if (!result) return {result.status,{}};
    } else {
        size_t count=0;
        for(const auto *own:{&e.first,&e.second})
            for(const auto &[key,item]:own->original.items) {
                (void)key;const auto &p=*ports_.players.find(own->player);
                if(personalItem(own->original,item,p.actor,p.persistent.containers.hirelingEquipment)) count+=identityCount(item);
            }
        if(!ports_.items.identityCapacity(count)) return {DomainStatus::Capacity,{}};
        std::vector<transactions::Change> changesToPrepare;
        for (const auto *own : {&e.first,&e.second}) {
            const auto &p=*ports_.players.find(own->player);
            auto inventory=p.persistent.inventory;
            auto rules=std::make_shared<EquipmentRules>(*p.rules.equipment);
            std::map<EntityId,EntityId> copies;
            // PlrSave::WriteItems stores this player's inventory chain. Corpses
            // and hirelings have separate chains and retain their live units.
            for(const auto key:inventoryItemOrder(own->original))
                if(personalItem(own->original,own->original.items.at(key),p.actor,p.persistent.containers.hirelingEquipment))
                    reserveCopy(own->original.items.at(key),ports_.items,copies);
            std::erase_if(inventory.items,[&](const auto &entry){return personalItem(inventory,entry.second,p.actor,p.persistent.containers.hirelingEquipment);});
            inventory.containers.erase(own->container);
            inventory.itemOrder=inventoryItemOrder(inventory);
            for(const auto key:inventoryItemOrder(own->original)) {
                if(!copies.contains(key)) continue;
                auto item=copyItem(own->original.items.at(key),copies);
                rules->items.emplace(item.id,own->equipment->items.at(key));
                inventory.itemOrder.push_back(item.id);
                inventory.items.emplace(item.id,std::move(item));
            }
            for(auto &[key,container]:inventory.containers) {
                (void)key;
                if(copies.contains(container.registeredWeapon)) container.registeredWeapon=copies.at(container.registeredWeapon);
            }
            auto change=edit(p,ports_.areas,tick,std::move(inventory)); change.equipment=std::move(rules);
            for(auto &owner:change.character->selectedSkillOwners)
                if(copies.contains(EntityId{owner})) owner=uint32_t(copies.at(EntityId{owner}).value);
            for(auto &hotkey:change.character->skillHotkeys)
                if(copies.contains(EntityId{hotkey.owner})) hotkey.owner=uint32_t(copies.at(EntityId{hotkey.owner}).value);
            for(const auto key:change.inventory.itemOrder) {
                if(p.persistent.inventory.items.contains(key)) continue;
                const auto &item=change.inventory.items.at(key);
                change.changes.push_back({key,item.revision,ItemChangeKind::Created,{},item.location,item.quantity});
            }
            auto restored=p.persistent; restored.inventory=change.inventory; restored.player=*change.character;
            change.facts.emplace_back(InventoryResetFact{restored});
            change.facts.emplace_back(TradeFact{12});
            changesToPrepare.emplace_back(std::move(change));
        }
        auto prepared=ports_.transactions.prepareGroup(std::move(changesToPrepare));if(!prepared) return {prepared.status,{}};
        const auto result=ports_.transactions.commitInventories(std::move(*prepared.value)); if(!result) return result;
        // Restored units have new identities; discard the removed units' clocks.
        for(const auto *own:{&e.first,&e.second})
            for(const auto &[key,item]:own->original.items) {
                const auto &p=*ports_.players.find(own->player);
                if(personalItem(own->original,item,p.actor,p.persistent.containers.hirelingEquipment)) ports_.items.clearRestoration(key);
            }
    }
    state_.cooldown.swap(cooldown); state_.exchanges.erase(e.transaction); return applied();
}
StepStatus System::step(TickContext tick) {
    std::erase_if(state_.cooldown,[&](const auto &entry){return entry.second<=tick.tick || !ports_.players.find(entry.first);});
    for (auto it=state_.exchanges.begin();it!=state_.exchanges.end();) {
        auto &e=it->second; const auto first=e.first.player; ++it;
        const auto *a=ports_.players.find(e.first.player), *b=ports_.players.find(e.second.player);
        if(!a || !b || !a->entered || !b->entered || a->area!=b->area || a->persistent.player.hp<=0 || b->persistent.player.hp<=0) {
            if(!cancelFor(first,tick.tick)) return StepStatus::Blocked;
        } else if(e.open && e.unlock && tick.tick>=e.unlock) {
            const auto sent=ports_.events.publishGroup(notifications(ports_.players,e,tick.tick,15));
            if(!sent) return StepStatus::Blocked;
            e.unlock=0;
        }
    }
    return StepStatus::Complete;
}
}

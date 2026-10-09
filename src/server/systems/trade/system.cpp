#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/inventory/eligibility.hpp"
#include "server/systems/npc/system.hpp"
#include <algorithm>

namespace d2x::server::trade {
namespace {
DomainResult<> applied() { return {DomainStatus::Applied,std::monostate{}}; }
const Offer &offer(const Exchange &e,PlayerId id) { return e.first.player==id?e.first:e.second; }
const Offer &other(const Exchange &e,PlayerId id) { return e.first.player==id?e.second:e.first; }
bool cursorEmpty(const PlayerState &p) {
    return std::none_of(p.persistent.inventory.items.begin(),p.persistent.inventory.items.end(),[&](const auto &entry){
        const auto *at=std::get_if<ContainerLocation>(&entry.second.location);
        return at && at->container==p.persistent.containers.cursor;
    });
}
ActorContext context(const PlayerState &p,const AreaStore &areas,uint64_t tick) {
    return {p.player,p.actor,p.area,areas.at(p.area).generation,0,tick};
}
std::vector<EntityId> offered(const InventoryState &inventory,EntityId container) {
    std::vector<EntityId> result;
    for (const auto &[id,item] : inventory.items) {
        const auto *at=std::get_if<ContainerLocation>(&item.location);
        if (at && at->container==container) result.push_back(id);
    }
    return result;
}
TradeItemsFact mirrors(const PlayerState &p,const Offer &own,const InventoryState &next) {
    TradeItemsFact fact; fact.projection.player=p.persistent.player;
    fact.projection.containers=p.persistent.containers;
    fact.projection.inventory.containers=next.containers;
    for (auto id : offered(p.persistent.inventory,own.container)) fact.removed.push_back(own.mirrors.at(id));
    const auto copy=[&](auto &&self,ItemInstance item)->ItemInstance {
        item.id=own.mirrors.at(item.id);
        for (auto &child : item.socketedItems) {
            child=self(self,std::move(child));
            std::get<SocketLocation>(child.location).host=item.id;
        }
        return item;
    };
    for (auto id : offered(next,own.container)) {
        auto item=copy(copy,next.items.at(id));
        fact.projection.inventory.items.emplace(item.id,std::move(item));
    }
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
        const auto &peer=other(e,own->player); const auto &p=*players.find(own->player);
        result.push_back({0,tick,{}, {AudienceKind::Player,own->player,p.area},
            {TradeFact{action,{},{},std::pair{own->gold,peer.gold}}}});
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
    if (const auto *e=find(id); e && e->open) save.inventory=offer(*e,id).original;
}
DomainResult<> System::decorate(PlayerId id,transactions::InventoryEdit &change) {
    const auto *e=find(id); if (!e) return applied();
    if (!canEdit(id) || e->revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    const auto &own=offer(*e,id), &peer=other(*e,id);
    const auto &p=*ports_.players.find(id);
    change.directed.push_back({peer.player,{mirrors(p,own,change.inventory)}});
    change.facts.emplace_back(TradeFact{6,{},{},std::pair{own.gold,peer.gold}});
    change.facts.emplace_back(TradeFact{14});
    change.directed.push_back({peer.player,{TradeFact{6,{},{},std::pair{peer.gold,own.gold}},TradeFact{14}}});
    return applied();
}
void System::edited(PlayerId id,uint64_t tick) noexcept {
    for (auto &[key,e] : state_.exchanges) if (e.first.player==id || e.second.player==id) {
        (void)key; e.first.agreed=e.second.agreed=false; ++e.revision; e.unlock=tick+250; return;
    }
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
        size_t identities=2;
        const auto count=[&](auto &&self,const ItemInstance &item)->void { ++identities; for(const auto &child:item.socketedItems) self(self,child); };
        for (const auto *player : {p,peer}) for (const auto &[id,item] : player->persistent.inventory.items) { (void)id; count(count,item); }
        if (!ports_.items.identityCapacity(identities)) return {DomainStatus::Capacity,{}};
        std::vector<transactions::Plan> plans;
        for (auto *o : {&next.first,&next.second}) {
            const auto &player=*ports_.players.find(o->player);
            if (!player.rules.character || !player.rules.equipment) return {DomainStatus::Unavailable,{}};
            o->container=ports_.items.reserveIdentity(); o->original=player.persistent.inventory; o->equipment=player.rules.equipment;
            const auto reserve=[&](auto &&self,const ItemInstance &item)->void {
                o->mirrors.emplace(item.id,ports_.items.reserveIdentity()); for (const auto &child:item.socketedItems) self(self,child);
            };
            for (const auto &[id,item] : o->original.items) { (void)id; reserve(reserve,item); }
            auto inventory=o->original;
            inventory.containers.emplace(o->container,ContainerState{o->container,{player.actor,ContainerKind::Trade,
                player.rules.character->tradeColumns,player.rules.character->tradeRows}});
            auto change=edit(player,ports_.areas,actor.tick,std::move(inventory));
            const auto &partner=*ports_.players.find(other(next,o->player).player);
            change.facts.emplace_back(TradeFact{6,partner.actor,partner.persistent.player.name,std::pair{0u,0u}});
            auto plan=ports_.transactions.prepare(std::move(change)); if(!plan) return {plan.status,{}};
            plans.push_back(std::move(*plan.value));
        }
        const auto result=ports_.transactions.commitInventories(std::move(plans)); if(!result) return result;
        next.open=true; ++next.revision; state_.exchanges.at(key)=std::move(next); return applied();
    }
    if (!next.open || next.revision==UINT64_MAX) return {DomainStatus::Conflict,{}};
    if (request.action==Action::Revoke || request.action==Action::OfferGold) {
        if (request.action==Action::OfferGold) {
            if (own.agreed || !cursorEmpty(*p) || request.gold>p->persistent.player.gold) return {DomainStatus::InvalidRequest,{}};
            const auto limit=uint64_t(peer->persistent.player.level)*10000;
            own.gold=unsigned(std::min<uint64_t>(request.gold,limit>peer->persistent.player.gold?limit-peer->persistent.player.gold:0));
        }
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
    for(size_t side=0;side<2;++side) for(auto id:offered(inventories[side],offers[side]->container)) {
        const auto &item=inventories[side].items.at(id);
        if(item.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        incoming[1-side].push_back(item);
        changes[side].push_back({id,item.revision+1,ItemChangeKind::Removed,item.location,{},0});
        inventories[side].items.erase(id);
        rules[1-side]->items.emplace(id,rules[side]->items.at(id)); rules[1-side]->includeSets(*rules[side]);
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
            const auto id=item.id; ++item.revision;
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
    std::vector<transactions::Plan> plans;
    for(size_t side=0;side<2;++side) {
        inventories[side].containers.erase(offers[side]->container);
        auto change=edit(*players[side],ports_.areas,tick,std::move(inventories[side]));
        change.changes=std::move(changes[side]); change.equipment=rules[side];
        change.character->gold=players[side]->persistent.player.gold-offers[side]->gold+offers[1-side]->gold;
        change.facts.emplace_back(mirrors(*players[1-side],*offers[1-side],InventoryState{}));
        change.facts.emplace_back(TradeFact{13});
        auto plan=ports_.transactions.prepare(std::move(change)); if(!plan) return {plan.status,{}};
        plans.push_back(std::move(*plan.value));
    }
    const auto result=ports_.transactions.commitInventories(std::move(plans)); if(!result) return result;
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
        std::vector<transactions::Plan> plans;
        for (const auto *own : {&e.first,&e.second}) {
            const auto &p=*ports_.players.find(own->player);
            auto change=edit(p,ports_.areas,tick,own->original); change.equipment=own->equipment;
            for (auto &[key,item] : change.inventory.items) {
                const auto current=p.persistent.inventory.items.find(key);
                if(current==p.persistent.inventory.items.end() || current->second.revision==UINT64_MAX) return {DomainStatus::Stale,{}};
                if(current->second.location==item.location) { item.revision=current->second.revision; continue; }
                item.revision=current->second.revision+1;
                change.changes.push_back({key,item.revision,ItemChangeKind::Moved,current->second.location,item.location,item.quantity});
            }
            const auto &peer=other(e,p.player);
            change.facts.emplace_back(mirrors(*ports_.players.find(peer.player),peer,InventoryState{}));
            change.facts.emplace_back(TradeFact{12});
            auto plan=ports_.transactions.prepare(std::move(change)); if(!plan) return {plan.status,{}};
            plans.push_back(std::move(*plan.value));
        }
        const auto result=ports_.transactions.commitInventories(std::move(plans)); if(!result) return result;
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

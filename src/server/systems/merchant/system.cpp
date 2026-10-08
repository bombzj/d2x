#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x::server::merchant {
DomainResult<> System::execute(const ActorContext &actor,const Request &request) {
    const auto *npc = ports_.npc.find(actor,request.npc.id,true);
    if (!npc) return {DomainStatus::InvalidRequest,{}};
    if (request.action==Action::Gamble) return {};
    if ((request.action==Action::IdentifyAll && !npc->rule.identify) ||
        ((request.action==Action::Repair || request.action==Action::RepairAll) && !npc->rule.repair) ||
        ((request.action==Action::Open || request.action==Action::Buy || request.action==Action::Sell) && !npc->rule.vendor)) return {DomainStatus::InvalidRequest,{}};
    if (state_.pending.contains(actor.player)) return {DomainStatus::Conflict,{}};
    if (state_.pending.size()>=8 || state_.next==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if (request.action==Action::Buy || request.action==Action::Sell || request.action==Action::Repair || request.action==Action::RepairAll) {
        const auto open=state_.opened.find(actor.player); if (open==state_.opened.end() || open->second!=npc->id) return {DomainStatus::InvalidRequest,{}};
    }
    state_.pending.emplace(actor.player,Pending{actor,request,state_.next++});
    return {DomainStatus::Applied,std::monostate{}};
}
std::vector<Preparation> System::pending() const {
    std::vector<Preparation> result;
    for (const auto &[id, pending] : state_.pending) {
        Preparation request; request.pending=pending;
        const auto *p=ports_.players.find(id); const auto *npc=ports_.npc.find(pending.actor,pending.request.npc.id,true);
        if (p && npc) { request.npc=npc->rule; request.character=p->persistent; request.inventoryRevision=p->inventoryRevision; request.characterRevision=p->characterRevision; request.reducedPrices=p->totals.character.combat.reducedPrices; }
        request.difficulty=ports_.settings.difficulty; request.seed=initialRandom(uint32_t(ports_.random ^ pending.token));
        if (const auto stock=state_.stocks.find(pending.request.npc.id); stock!=state_.stocks.end()) request.stock=stock->second;
        result.push_back(std::move(request));
    }
    return result;
}
std::optional<PersistentCharacter> System::shop(PlayerId id) const {
    const auto open=state_.opened.find(id); const auto *player=ports_.players.find(id);
    if (!player || open==state_.opened.end()) return {};
    const auto conversation=ports_.npc.read().conversations.find(id);
    if (conversation==ports_.npc.read().conversations.end() || conversation->second.npc!=open->second || conversation->second.area!=player->area || player->persistent.player.hp<=0) return {};
    const auto stock=state_.stocks.find(open->second); if (stock==state_.stocks.end()) return {};
    PersistentCharacter projection; projection.player=player->persistent.player; projection.player.id=open->second;
    projection.containers.backpack=EntityId{1}; projection.inventory.containers.emplace(EntityId{1},ContainerState{EntityId{1},{open->second,ContainerKind::Backpack,16,16}});
    for (const auto &[item,offer] : stock->second.offers) { auto copy=offer.item; copy.location=ContainerLocation{EntityId{1},{}}; projection.inventory.items.emplace(item,std::move(copy)); }
    return projection;
}
DomainResult<> System::install(Prepared prepared) {
    const auto &source=prepared.source; const auto &pending=source.pending; const auto &request=pending.request; const auto &actor=pending.actor;
    const auto queued=state_.pending.find(actor.player);
    if (queued==state_.pending.end() || queued->second.token!=pending.token) return {DomainStatus::Stale,{}};
    const auto *player=ports_.players.find(actor.player);
    const auto operation=uint8_t(request.action==Action::Sell?3:request.action==Action::Repair || request.action==Action::RepairAll?1:5);
    const auto failure=[&](DomainStatus status,uint8_t result=9)->DomainResult<> {
        if (player && player->entered) { auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,player->area},{MerchantFact{request.npc.id,request.item?request.item->id:EntityId{},0,result,player->persistent.player.gold}}}); if (!sent) return {sent.status,{}}; }
        state_.pending.erase(queued); return {status,{}};
    };
    if (!player || !ports_.npc.find(actor,request.npc.id,true)) return failure(DomainStatus::InvalidActor);
    if (source.inventoryRevision!=player->inventoryRevision || source.characterRevision!=player->characterRevision) return failure(DomainStatus::Stale);
    if (!prepared.deferred.empty()) { state_.deferred=std::move(prepared.deferred); return failure(DomainStatus::Unavailable); }
    if (request.action==Action::Open) {
        auto stocks=state_.stocks; auto opened=state_.opened;
        if (!stocks.contains(request.npc.id)) {
            Stock stock;
            if (prepared.offers.size()>1024) return failure(DomainStatus::Capacity);
            for (auto &offer : prepared.offers) { offer.item.id=ports_.items.reserveIdentity(); offer.item.revision=1; stock.offers.emplace(offer.item.id,std::move(offer)); }
            stocks.emplace(request.npc.id,std::move(stock));
        }
        opened[actor.player]=request.npc.id;
        auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{MerchantFact{request.npc.id,{},0,0,player->persistent.player.gold,true}}}); if (!sent) return {sent.status,{}};
        state_.stocks.swap(stocks); state_.opened.swap(opened); state_.pending.erase(queued); return {DomainStatus::Applied,std::monostate{}};
    }
    if (!ports_.definitions || !player->rules.equipment || !player->rules.character) return failure(DomainStatus::Unavailable);
    auto rules=std::make_shared<EquipmentRules>(*player->rules.equipment);
    inventory::detail::Draft draft(*player,*ports_.definitions,*rules,*player->rules.character);
    auto record=player->persistent.player; uint64_t total=0; EntityId soldOffer;
    const auto accessible=[&](const ItemInstance &item) { const auto *at=std::get_if<ContainerLocation>(&item.location); const auto &c=player->persistent.containers; return at && (at->container==c.backpack || at->container==c.equipment || at->container==c.beltEquipment || (request.action==Action::Sell && at->container==c.cursor)); };
    if (request.action==Action::Buy) {
        const auto stock=state_.stocks.find(request.npc.id);
        if (!request.item || stock==state_.stocks.end()) return failure(DomainStatus::Stale);
        const auto offer=stock->second.offers.find(request.item->id);
        if (offer==stock->second.offers.end() || (request.item->revision && request.item->revision!=offer->second.item.revision) || !prepared.prices.contains(request.item->id)) return failure(DomainStatus::Stale);
        total=prepared.prices.at(request.item->id); auto item=offer->second.item; item.id=ports_.items.reserveIdentity(); item.revision=1;
        item.location=ContainerLocation{player->persistent.containers.cursor,{}};
        draft.edit.inventory.items.emplace(item.id,item); rules->items.emplace(item.id,offer->second.equipment);
        const auto destination=draft.space(item.id,player->persistent.containers.backpack); if (!destination) return failure(DomainStatus::Capacity);
        draft.edit.inventory.items.at(item.id).location=*destination;
        draft.edit.changes.push_back({item.id,1,ItemChangeKind::Created,{},ItemLocation{*destination},item.quantity});
        if (!offer->second.permanent) soldOffer=offer->first;
    } else if (request.action==Action::Sell) {
        if (!request.item || !prepared.prices.contains(request.item->id)) return failure(DomainStatus::InvalidRequest);
        const auto *item=draft.resolve(*request.item); if (!item || !accessible(*item)) return failure(DomainStatus::Stale);
        if (item->revision==UINT64_MAX) return failure(DomainStatus::Capacity);
        const auto copy=*item; const auto price=prepared.prices.at(copy.id); const auto cap=unsigned(record.level)*10000;
        if (record.gold>cap || price>cap-record.gold) return failure(DomainStatus::Capacity);
        const bool belt=std::get<ContainerLocation>(copy.location).container==player->persistent.containers.beltEquipment;
        draft.edit.changes.push_back({copy.id,copy.revision+1,ItemChangeKind::Removed,copy.location,{},0}); draft.edit.inventory.items.erase(copy.id); rules->items.erase(copy.id);
        if (belt && draft.resizeBelt(1)!=DomainStatus::Applied) return failure(DomainStatus::Capacity);
        record.gold+=price;
    } else {
        for (const auto &[id,price] : prepared.prices) {
            auto found=draft.edit.inventory.items.find(id); if (found==draft.edit.inventory.items.end() || !accessible(found->second) || found->second.revision==UINT64_MAX) return failure(DomainStatus::Stale);
            if (request.item && (request.item->id!=id || request.item->revision!=found->second.revision)) return failure(DomainStatus::Stale);
            auto &item=found->second; const auto *definition=ports_.definitions->find(item.definition); if (!definition) return failure(DomainStatus::Unavailable);
            if (request.action==Action::IdentifyAll) { item.identified=true; item.nativeFlags|=0x10; }
            else { const auto &values=rules->at(id,record.level); item.durability=values.maximumDurability;
                if (definition->equipment.throwable && definition->equipment.repairable) { int extra=0; for(const auto &stat:values.stats) if(stat.effect=="item_extra_stack") extra+=stat.value; item.quantity=unsigned(std::clamp(int(definition->maxStack)+extra,1,511)); }
            }
            ++item.revision; draft.edit.changes.push_back({id,item.revision,ItemChangeKind::PropertiesChanged,item.location,item.location,item.quantity}); total+=price;
        }
    }
    if (total>uint64_t(record.gold)+record.bankGold) return failure(DomainStatus::Conflict,12);
    const unsigned wallet=unsigned(std::min<uint64_t>(record.gold,total)); record.gold-=wallet; record.bankGold-=unsigned(total-wallet);
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),record.weaponSet};
    edit.equipment=std::move(rules); edit.character=record; edit.facts.emplace_back(MerchantFact{request.npc.id,request.item?request.item->id:EntityId{},operation,uint8_t(request.action==Action::Sell?1:request.action==Action::Repair || request.action==Action::RepairAll?2:0),record.gold});
    auto plan=ports_.transactions.prepare(std::move(edit)); if (!plan) return failure(plan.status);
    const auto result=ports_.transactions.commit(std::move(*plan.value)); if (!result) return {result.status,{}};
    if (soldOffer) { auto &stock=state_.stocks.at(request.npc.id); stock.offers.erase(soldOffer); ++stock.revision; }
    state_.pending.erase(queued); return result;
}
}

namespace d2x::server::merchant {
StepStatus System::step(TickContext, FrameFacts &) {
    const auto valid = [&](PlayerId id, EntityId npc) {
        const auto *player = ports_.players.find(id);
        const auto conversation = ports_.npc.read().conversations.find(id);
        return player && player->entered && player->persistent.player.hp > 0 &&
            conversation != ports_.npc.read().conversations.end() && conversation->second.npc == npc &&
            conversation->second.area == player->area;
    };
    std::erase_if(state_.opened, [&](const auto &entry) { return !valid(entry.first, entry.second); });
    std::erase_if(state_.pending, [&](const auto &entry) { return !valid(entry.first, entry.second.request.npc.id); });
    return StepStatus::Complete;
}
}

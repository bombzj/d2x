#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "core/random.hpp"
#include "server/systems/loot/system.hpp"
#include <algorithm>
namespace d2x::server::merchant {
std::optional<Cell> shelfPosition(const Stock &stock,const ItemDefinition &base,const ItemCatalog &catalog,int page) {
    if(stock.columns<=0 || stock.rows<=0 || base.width<=0 || base.height<=0) return {};
    for(int column=0;column<stock.columns;++column) {
        const int x=base.height==1?stock.columns-column-1:column;
        if(x+base.width>stock.columns) continue;
        for(int y=0;y+base.height<=stock.rows;++y) {
            bool clear=true;
            for(const auto &[id,offer]:stock.offers) {
                (void)id;if(offer.storePage!=page) continue;
                const auto *other=catalog.find(offer.item.definition);
                if(!other) return {};
                if(x<offer.cell.x+other->width && x+base.width>offer.cell.x &&
                    y<offer.cell.y+other->height && y+base.height>offer.cell.y) {clear=false;break;}
            }
            if(clear) return Cell{x,y};
        }
    }
    return {};
}
const Stock *System::stock(PlayerId player, EntityId npc, bool gamble) const {
    if (gamble) {
        const auto found = state_.gambles.find({gambleOwner(player,npc), npc});
        return found == state_.gambles.end() ? nullptr : &found->second;
    }
    const auto found = state_.stocks.find(npc);
    return found == state_.stocks.end() ? nullptr : &found->second;
}
DomainResult<> System::execute(const ActorContext &actor,const Request &request) {
    if(request.action==Action::IdentifyBought) {
        const auto *p=ports_.players.find(actor.player);
        const auto bought=state_.bought.find(actor.player);
        if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
        if(!request.item || bought==state_.bought.end() || bought->second!=request.item->id) return {DomainStatus::InvalidRequest,{}};
        const auto found=p->persistent.inventory.items.find(request.item->id);
        if(found==p->persistent.inventory.items.end() || found->second.revision!=request.item->revision) return {DomainStatus::Stale,{}};
        if(found->second.identified) return {DomainStatus::Applied,std::monostate{}};
        if(found->second.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto inventory=p->persistent.inventory;auto &item=inventory.items.at(request.item->id);
        item.identified=true;item.nativeFlags|=0x10u;++item.revision;
        std::vector<ItemChange> changes{{item.id,item.revision,ItemChangeKind::PropertiesChanged,item.location,item.location,item.quantity}};
        transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(inventory),
            std::move(changes),p->persistent.player.weaponSet};
        auto plan=ports_.transactions.prepare(std::move(edit));
        return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
    }
    const auto access = ports_.npc.service(actor, request.npc.id);
    if (!access) return {DomainStatus::InvalidRequest,{}};
    const auto *npc = access->npc;
    if(npc->rule.pd2EventGheed) state_.sharedGheeds.insert(npc->id);
    const auto *conversation = access->conversation;
    if ((request.action==Action::Gamble || request.gamble) && !npc->rule.gamble) return {DomainStatus::InvalidRequest,{}};
    if ((request.action==Action::IdentifyAll && !npc->rule.identify) ||
        ((request.action==Action::Repair || request.action==Action::RepairAll) && !npc->rule.repair) ||
        ((request.action==Action::Open || (request.action==Action::Buy && !request.gamble) || request.action==Action::Sell) && !npc->rule.vendor)) return {DomainStatus::InvalidRequest,{}};
    if (state_.pending.contains(actor.player)) return {DomainStatus::Conflict,{}};
    if (state_.pending.size()>=8 || state_.next==UINT64_MAX) return {DomainStatus::Capacity,{}};
    if (request.action==Action::Buy || request.action==Action::Sell || request.action==Action::Repair || request.action==Action::RepairAll) {
        const auto open=state_.opened.find(actor.player); if (open==state_.opened.end() || (open->second.npc!=npc->id || open->second.conversation!=conversation->revision)) return {DomainStatus::InvalidRequest,{}};
    }
    if(request.action==Action::Buy && state_.gambling.contains(actor.player)!=request.gamble) return {DomainStatus::InvalidRequest,{}};
    state_.pending.emplace(actor.player,Pending{actor,request,state_.next++,conversation->revision});
    return {DomainStatus::Applied,std::monostate{}};
}
std::vector<Preparation> System::pending() const {
    std::vector<Preparation> result;
    for (const auto &[id, pending] : state_.pending) {
        Preparation request; request.pending=pending;
        const auto *p=ports_.players.find(id);
        const auto access=ports_.npc.service(pending.actor,pending.request.npc.id);
        if (!p || !access || access->conversation->revision!=pending.conversation) continue;
        const auto *npc=access->npc;
        request.npc=npc->rule; request.character=p->persistent; request.inventoryRevision=p->inventoryRevision; request.characterRevision=p->characterRevision; request.reducedPrices=p->totals.character.combat.reducedPrices;
        request.difficulty=ports_.settings.difficulty;
        request.uniques=ports_.loot.read().uniques;
        if (const auto *current = stock(id, pending.request.npc.id, pending.request.gamble);
            current && (!pending.request.gamble || npc->rule.pd2EventGheed || current->conversation == pending.conversation)) request.stock = *current;
        if(pending.request.action==Action::Open && request.stock.refreshPending && std::count_if(ports_.npc.read().conversations.begin(),ports_.npc.read().conversations.end(),[&](const auto &entry){return entry.second.npc==pending.request.npc.id;})<=1) {request.stock.offers.clear();request.stock.generated=false;}
        if((pending.request.action==Action::Open || pending.request.action==Action::Gamble) && !request.stock.generated) {
            request.controlRandom=ports_.npc.read().controlRandom;request.gameRandom=ports_.random;
        }
        if(npc->rule.pd2EventGheed) request.pd2Random=ports_.pd2Random;
        result.push_back(std::move(request));
    }
    return result;
}
std::optional<PersistentCharacter> System::shop(PlayerId id) const {
    const auto open=state_.opened.find(id); const auto *player=ports_.players.find(id);
    if (!player || open==state_.opened.end()) return {};
    const auto *conversation=ports_.npc.conversation(id);
    if (!conversation || conversation->npc!=open->second.npc || conversation->revision!=open->second.conversation) return {};
    const Stock *stock=this->stock(id,open->second.npc,state_.gambling.contains(id));
    if(!stock) return {};
    PersistentCharacter projection; projection.player=player->persistent.player; projection.player.id=open->second.npc;
    projection.containers.backpack=EntityId{1};
    for(int page=0;page<4;++page) {
        const EntityId container{uint64_t(page+1)};
        projection.inventory.containers.emplace(container,ContainerState{container,{open->second.npc,ContainerKind::Backpack,stock->columns,stock->rows}});
    }
    for (const auto &[item,offer] : stock->offers) { auto copy=offer.item; if(state_.gambling.contains(id)) {copy.identified=false;copy.nativeFlags|=0x2000000u;if(!offer.displayCode.empty()) copy.definition=offer.displayCode;copy.quality=ItemQuality::Normal;copy.affixes.clear();copy.specialRow=-1;copy.runewordRow=-1;copy.socketedItems.clear();copy.nativeProperties=false;} copy.location=ContainerLocation{EntityId{uint64_t(std::max(0,offer.storePage)+1)},offer.cell}; projection.inventory.items.emplace(item,std::move(copy)); }
    return projection;
}
DomainResult<> System::install(Prepared prepared) {
    const auto &source=prepared.source;
    const auto queued=state_.pending.find(source.pending.actor.player);
    if (queued==state_.pending.end() || queued->second.token!=source.pending.token) return {DomainStatus::Stale,{}};
    const auto &pending=queued->second;const auto &request=pending.request;const auto &actor=pending.actor;
    const auto *player=ports_.players.find(actor.player);
    auto operation=uint8_t(request.action==Action::Buy?4:request.action==Action::Sell?3:request.action==Action::Repair || request.action==Action::RepairAll?1:5);
    const auto failure=[&](DomainStatus status,uint8_t result=9)->DomainResult<> {
        if (player && player->entered) { auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,player->area},{MerchantFact{request.npc.id,EntityId{UINT32_MAX},0,result,player->persistent.player.gold}}}); if (!sent) return {sent.status,{}}; }
        state_.pending.erase(queued); return {status,{}};
    };
    const auto access=ports_.npc.service(actor,request.npc.id);
    if (!player || !access || access->conversation->revision!=pending.conversation) return failure(DomainStatus::InvalidActor);
    if (source.inventoryRevision!=player->inventoryRevision || source.characterRevision!=player->characterRevision) return failure(DomainStatus::Stale);
    if(source.controlRandom && (ports_.npc.read().controlRandom!=source.controlRandom || ports_.random!=source.gameRandom))
        return {DomainStatus::Stale,{}};
    if(prepared.pd2Random && source.pd2Random!=ports_.pd2Random) return {DomainStatus::Stale,{}};
    if (!prepared.deferred.empty()) { state_.deferred=std::move(prepared.deferred); return failure(DomainStatus::Unavailable); }
    // All shop transactions publish at most inventory, character and result.
    // Preserve the pending purchase and seed while reliable output is full.
    if (!ports_.events.hasCapacity(3)) return {DomainStatus::Capacity,{}};
    if (request.action==Action::Open || request.action==Action::Gamble) {
        const auto identityCursor=ports_.items.identityCursor();
        size_t identityCount=0;
        if(!source.stock.generated &&
            (!source.controlRandom || !source.gameRandom || !prepared.controlRandom || !prepared.gameRandom)) return failure(DomainStatus::InvalidRequest);
        auto stocks=state_.stocks; auto gambles=state_.gambles; auto opened=state_.opened; auto gambling=state_.gambling;
        auto uniques=ports_.loot.prepareUniques(prepared.limitedUniques);
        const auto owner=gambleOwner(actor.player,request.npc.id);
        const bool exists=request.gamble?gambles.contains({owner,request.npc.id}):stocks.contains(request.npc.id);
        const Stock *previous=request.gamble?(exists?&gambles.at({owner,request.npc.id}):nullptr):(exists?&stocks.at(request.npc.id):nullptr);
        if (request.gamble && !source.npc.pd2EventGheed && previous && previous->conversation!=pending.conversation) previous=nullptr;
        if(source.stock.generated && (!previous || !previous->generated)) return {DomainStatus::Stale,{}};
        if(previous && previous->revision!=source.stock.revision) return failure(DomainStatus::Stale);
        if(!request.gamble && previous && !source.stock.generated && previous->refreshPending &&
            std::count_if(ports_.npc.read().conversations.begin(),ports_.npc.read().conversations.end(),[&](const auto &entry){return entry.second.npc==request.npc.id;})>1) return failure(DomainStatus::Conflict);
        const bool refresh=!request.gamble && previous && previous->refreshPending && !source.stock.generated;
        if(!request.gamble && source.controlRandom && previous && previous->generated && !refresh)
            return {DomainStatus::Stale,{}};
        if (!previous || !previous->generated || refresh) {
            Stock stock=refresh?Stock{}:previous?*previous:Stock{};stock.area=actor.area;stock.generated=true;stock.refreshAt=actor.tick+6000;
            if(prepared.columns<=0 || prepared.columns>255 || prepared.rows<=0 || prepared.rows>255) return failure(DomainStatus::InvalidRequest);
            stock.columns=prepared.columns;stock.rows=prepared.rows;
            if(refresh) {if(previous->revision==UINT64_MAX) return failure(DomainStatus::Capacity);stock.revision=previous->revision+1;}
            if (request.gamble) stock.conversation=pending.conversation;
            if(prepared.equipment) stock.setRules.includeSets(*prepared.equipment);
            const auto allocations=source.controlRandom?prepared.allocatedItems:prepared.offers.size();
            if (prepared.offers.size()>1024 || allocations<prepared.offers.size() || !ports_.items.identityCapacity(allocations)) return failure(DomainStatus::Capacity);
            if(source.controlRandom) {
                uint64_t previousIdentity=0;
                std::map<int,std::vector<bool>> occupied;
                for(const auto &offer:prepared.offers) {
                    if(offer.item.id.value<=previousIdentity || offer.item.id.value>allocations) return failure(DomainStatus::InvalidRequest);
                    const auto *base=ports_.definitions?ports_.definitions->find(offer.item.definition):nullptr;
                    if(!base || !prepared.equipment || !prepared.equipment->items.contains(offer.item.id) ||
                        offer.storePage<0 || offer.storePage>3 || (request.gamble && offer.storePage!=0) ||
                        base->width<=0 || base->height<=0 || offer.cell.x<0 || offer.cell.y<0 ||
                        offer.cell.x+base->width>stock.columns || offer.cell.y+base->height>stock.rows ||
                        !offer.item.socketedItems.empty()) return failure(DomainStatus::InvalidRequest);
                    auto &cells=occupied.try_emplace(offer.storePage,size_t(stock.columns)*size_t(stock.rows),false).first->second;
                    for(int dy=0;dy<base->height;++dy) for(int dx=0;dx<base->width;++dx) {
                        const auto cell=size_t(offer.cell.y+dy)*size_t(stock.columns)+size_t(offer.cell.x+dx);
                        if(cells[cell]) return failure(DomainStatus::InvalidRequest);
                        cells[cell]=true;
                    }
                    previousIdentity=offer.item.id.value;
                }
            }
            size_t allocated=0;
            for (auto &offer : prepared.offers) {
                const auto target=source.controlRandom?size_t(offer.item.id.value):allocated+1;
                allocated=target;offer.item.id=EntityId{identityCursor+target-1};
                offer.item.revision=1;stock.offers.emplace(offer.item.id,std::move(offer));
            }
            identityCount=allocations;
            if(request.gamble) gambles.insert_or_assign(std::pair{owner,request.npc.id},std::move(stock));
            else stocks.insert_or_assign(request.npc.id,std::move(stock));
        }
        opened[actor.player]={request.npc.id,pending.conversation};
        if(request.gamble) gambling.insert(actor.player);else gambling.erase(actor.player);
        std::optional<npc::VendorControlPublication> control;
        if(source.controlRandom) {
            auto rule=access->npc->rule;if(request.gamble) rule.hirelingNames.clear();
            auto candidate=ports_.npc.prepareVendorControl(*source.controlRandom,*prepared.controlRandom,rule);
            if(!candidate) return {candidate.status,{}};
            control=std::move(*candidate.value);
        }
        auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{MerchantFact{request.npc.id,{},0,0,player->persistent.player.gold,true}}}); if (!sent) return {sent.status,{}};
        if(identityCount) ports_.items.publishIdentities(identityCursor,identityCount);
        state_.stocks.swap(stocks);state_.gambles.swap(gambles);state_.opened.swap(opened);state_.gambling.swap(gambling);
        if(source.controlRandom) {
            ports_.npc.publishVendorControl(std::move(*control));
            ports_.random=*prepared.gameRandom;
        }
        if(prepared.pd2Random) ports_.pd2Random=*prepared.pd2Random;
        ports_.loot.commitUniques(std::move(uniques));state_.pending.erase(queued);return {DomainStatus::Applied,std::monostate{}};
    }
    if (!ports_.definitions || !player->rules.equipment || !player->rules.character) return failure(DomainStatus::Unavailable);
    auto rules=std::make_shared<EquipmentRules>(*player->rules.equipment);
    inventory::detail::Draft draft(*player,*ports_.definitions,*rules,*player->rules.character);
    auto record=player->persistent.player; uint64_t total=0; EntityId soldOffer;
    const auto itemIdentityCursor=ports_.items.identityCursor();
    size_t itemIdentityCount=0;
    auto itemRandom=ports_.random;
    const auto allocateItem=[&]() {
        const EntityId identity{itemIdentityCursor+itemIdentityCount++};
        rollRandom(itemRandom);rollRandom(itemRandom);
        return identity;
    };
    EntityId receiptItem=request.item?request.item->id:EntityId{};
    std::optional<std::map<EntityId,Stock>> changedStocks;
    const auto accessible=[&](const ItemInstance &item) { const auto *at=std::get_if<ContainerLocation>(&item.location); const auto &c=player->persistent.containers; return at && (at->container==c.backpack || at->container==c.equipment || at->container==c.beltEquipment || (request.action==Action::Sell && at->container==c.cursor)); };
    if (request.action==Action::Buy) {
        const Stock *stock=this->stock(actor.player,request.npc.id,request.gamble);
        if(!request.item || !stock) return failure(DomainStatus::Stale);
        if (stock->revision != source.stock.revision || stock->revision == UINT64_MAX) return failure(DomainStatus::Stale);
        const auto offer=stock->offers.find(request.item->id);
        if (offer==stock->offers.end() || (request.item->revision && request.item->revision!=offer->second.item.revision) || !prepared.prices.contains(request.item->id)) return failure(DomainStatus::Stale);
        if(draft.at({player->persistent.containers.cursor,{}})) return failure(DomainStatus::Conflict);
        const auto *offerDefinition=ports_.definitions->find(offer->second.item.definition);
        if (!offerDefinition) return failure(DomainStatus::Unavailable);
        if(offer->second.equipment.singleCarry || offerDefinition->opensCube) for(const auto &[id,owned]:player->persistent.inventory.items) {
            (void)id;const auto *at=std::get_if<ContainerLocation>(&owned.location);
            if(at && player->persistent.inventory.containers.at(at->container).spec.kind!=ContainerKind::Corpse &&
                (offerDefinition->opensCube ? owned.definition==offer->second.item.definition : owned.quality==ItemQuality::Unique && owned.specialRow==offer->second.item.specialRow)) return failure(DomainStatus::Conflict);
        }
        rules->includeSets(stock->setRules);
        if(!ports_.items.identityCapacity(itemIdentityCount+1+offer->second.item.socketedItems.size())) return failure(DomainStatus::Capacity);
        total=prepared.prices.at(request.item->id); auto item=prepared.purchase.value_or(offer->second.item);
        item.identified=true;item.nativeFlags=(item.nativeFlags&~0x2000000u)|0x10u; item.id=allocateItem(); item.revision=1;
        receiptItem=item.id;
        for(size_t index=0;index<item.socketedItems.size();++index) {auto &child=item.socketedItems[index];child.id=allocateItem();child.revision=1;child.location=SocketLocation{item.id,unsigned(index)};}
        item.location=ContainerLocation{player->persistent.containers.cursor,{}};
        draft.edit.inventory.items.emplace(item.id,item);
        rules->items.emplace(item.id,prepared.purchase && prepared.equipment?prepared.equipment->items.at(request.item->id):offer->second.equipment);
        const auto *base=ports_.definitions->find(item.definition); if(!base) return failure(DomainStatus::Unavailable);
        // Native scroll purchases fill a compatible book before taking space;
        // permanent potion multibuy fills the remaining belt, never the pack.
        const uint64_t funds=uint64_t(record.gold)+record.bankGold;
        if(request.multibuy && offer->second.permanent && base->autoStack && !base->bookCapacity && prepared.unitPrices.contains(request.item->id)) {
            const auto &values=offer->second.equipment.levels.at(size_t(record.level));
            int extra=0;for(const auto &stat:values.stats) if(stat.effect=="item_extra_stack") extra+=stat.value;
            unsigned cap=unsigned(std::clamp(int(base->maxStack)+extra,1,511));
            for(const auto &[id,target]:draft.edit.inventory.items) if(id!=item.id)
                if(const auto remaining=draft.stackSpace(item,target)) {cap=remaining;break;}
            const unsigned unit=prepared.unitPrices.at(request.item->id);
            const unsigned amount=unsigned(std::min<uint64_t>(cap,unit?funds/unit:cap));
            if(!amount) return failure(DomainStatus::Conflict,12);
            item.quantity=amount;draft.edit.inventory.items.at(item.id).quantity=amount;total=uint64_t(unit)*amount;
        }
        if(total>funds) return failure(DomainStatus::Conflict,12);
        bool bookFilled=false;
        if(base->equipment.isType("scro")) for(auto &[id,target]:draft.edit.inventory.items) {
            const auto *at=std::get_if<ContainerLocation>(&target.location);
            const auto *book=ports_.definitions->find(target.definition);
            if(id==item.id || !at || at->container!=player->persistent.containers.backpack || !book || book->bookScroll!=base->code || target.charges>=book->bookCapacity) continue;
            const unsigned amount=request.multibuy && offer->second.permanent?unsigned(std::min<uint64_t>(book->bookCapacity-target.charges,total?funds/total:book->bookCapacity-target.charges)):1;
            if(!amount || target.revision==UINT64_MAX) return failure(DomainStatus::Capacity);
            target.charges+=amount;++target.revision;total*=amount;
            draft.edit.changes.push_back({id,target.revision,ItemChangeKind::QuantityChanged,target.location,target.location,target.quantity});
            receiptItem=id;
            draft.edit.inventory.items.erase(item.id);rules->items.erase(item.id);bookFilled=true;break;
        }
        if(!bookFilled && base->autoStack && !base->bookCapacity) {
            std::vector<EntityId> targets;
            for(const auto &[id,target]:draft.edit.inventory.items) if(id!=item.id && target.definition==item.definition) targets.push_back(id);
            for(const auto id:targets) {
                const auto from=draft.edit.inventory.items.find(item.id);if(from==draft.edit.inventory.items.end()) break;
                draft.merge({from->second.handle(),draft.edit.inventory.items.at(id).handle(),0});
                if(!draft.edit.inventory.items.contains(item.id)) receiptItem=id;
            }
            std::erase_if(draft.edit.changes,[&](const auto &v){return v.item==item.id;});
            bookFilled=!draft.edit.inventory.items.contains(item.id);
            if(bookFilled) rules->items.erase(item.id);
            else item=draft.edit.inventory.items.at(item.id);
        }
        if(!bookFilled) {
            std::optional<ContainerLocation> destination;
            const auto equipped=draft.autoEquip(item.id);
            if(!equipped) return failure(equipped.status);
            std::erase_if(draft.edit.changes,[&](const auto &change){return change.item==item.id;});
            if(*equipped.value) destination=std::get<ContainerLocation>(draft.edit.inventory.items.at(item.id).location);
            if(!destination && base->autoBelt) destination=draft.space(item.id,player->persistent.containers.belt);
            if(!destination) destination=draft.space(item.id,player->persistent.containers.backpack);
            if (!destination) return failure(DomainStatus::Capacity);
            auto &bought=draft.edit.inventory.items.at(item.id);
            bought.location=*destination;
            draft.edit.changes.push_back({bought.id,bought.revision,ItemChangeKind::Created,{},ItemLocation{*destination},bought.quantity});
            if(request.multibuy && offer->second.permanent && base->autoBelt && destination->container==player->persistent.containers.belt) {
                const uint64_t unitPrice=total;
                for(unsigned i=0;i<15 && (!unitPrice || total+unitPrice<=funds);++i) {
                    if(!ports_.items.identityCapacity(itemIdentityCount+1)) return failure(DomainStatus::Capacity);
                    auto extra=item;extra.id=allocateItem();extra.location=ContainerLocation{player->persistent.containers.cursor,{}};
                    draft.edit.inventory.items.emplace(extra.id,extra);
                    const auto at=draft.space(extra.id,player->persistent.containers.belt);
                    if(!at) {draft.edit.inventory.items.erase(extra.id);break;}
                    draft.edit.inventory.items.at(extra.id).location=*at;rules->items.emplace(extra.id,offer->second.equipment);
                    draft.edit.changes.push_back({extra.id,1,ItemChangeKind::Created,{},ItemLocation{*at},extra.quantity});total+=unitPrice;
                }
            }
        } else operation=5;
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
        // Prepare the NPC's bought item before committing the seller's inventory.
        // Stock publication uses the same original shop packet for every viewer.
        if(prepared.resaleDuplicate && !state_.gambling.contains(actor.player)) {
        changedStocks=state_.stocks;
        auto &stock=(*changedStocks)[request.npc.id];stock.area=actor.area;
        if(stock.revision==UINT64_MAX) return failure(DomainStatus::Capacity);
        // SUnitNpc duplicates the sold item into NPC stock. The seller's
        // removal and the new shop item must not share a unit identity.
        if(!copy.socketedItems.empty()) return failure(DomainStatus::InvalidRequest);
        if(!ports_.items.identityCapacity(itemIdentityCount+1)) return failure(DomainStatus::Capacity);
        auto bought=copy;bought.id=allocateItem();bought.revision=1;
        int page=prepared.resalePage;
        const auto *base=ports_.definitions->find(copy.definition);
        auto cell=base && page>=0 && page<=3?shelfPosition(stock,*base,*ports_.definitions,page):std::optional<Cell>{};
        if(!cell && base && page==1) {page=2;cell=shelfPosition(stock,*base,*ports_.definitions,page);}
        if(cell) {
            bought.identified=true;bought.nativeFlags|=0x10u;
            const auto &values=player->rules.equipment->items.at(copy.id);
            const auto resolved=player->rules.equipment->at(copy.id,record.level);
            bought.durability=resolved.maximumDurability;
            int extra=0;for(const auto &stat:resolved.stats) if(stat.effect=="item_extra_stack") extra+=stat.value;
            bought.quantity=unsigned(std::clamp(int(base->maxStack)+extra,1,511));
            stock.setRules.includeSets(*player->rules.equipment);
            stock.offers.emplace(bought.id,Offer{std::move(bought),values,false,{},page,*cell});
            ++stock.revision;
        }
        }
    } else {
        if(request.action==Action::Repair && (!request.item || !prepared.prices.contains(request.item->id))) return failure(DomainStatus::InvalidRequest,9);
        for (const auto &[id,price] : prepared.prices) {
            auto found=draft.edit.inventory.items.find(id); if (found==draft.edit.inventory.items.end() || !accessible(found->second) || found->second.revision==UINT64_MAX) return failure(DomainStatus::Stale);
            if (request.item && (request.item->id!=id || request.item->revision!=found->second.revision)) return failure(DomainStatus::Stale);
            auto &item=found->second; const auto *definition=ports_.definitions->find(item.definition); if (!definition) return failure(DomainStatus::Unavailable);
            const bool partial=request.action==Action::Repair && price>uint64_t(record.gold)+record.bankGold;
            if (request.action==Action::IdentifyAll) { item.identified=true; item.nativeFlags|=0x10; }
            else if(partial) {
                // SUnitNpc's partial repair uses wallet gold and a 10-bit unit cost.
                // It does not restore charges, stacks, or a broken item.
                const auto maximum=rules->at(id,record.level).maximumDurability;
                if(!record.gold || item.durability>=maximum || (item.nativeFlags&0x100u)) return failure(DomainStatus::Conflict,12);
                const uint64_t divisor=(uint64_t(price)<<10)/(maximum-item.durability);
                const uint64_t budget=uint64_t(record.gold)<<10;
                if(!divisor || divisor>=budget) return failure(DomainStatus::Conflict,12);
                item.durability=unsigned(std::min<uint64_t>(maximum,uint64_t(item.durability)+budget/divisor));
            } else { if(prepared.repairs.contains(id)) { const auto location=item.location;const auto revision=item.revision;item=prepared.repairs.at(id);item.id=id;item.location=location;item.revision=revision;if(prepared.equipment) rules->items.insert_or_assign(id,prepared.equipment->items.at(id)); }
                const auto &values=rules->at(id,record.level); item.durability=values.maximumDurability;item.nativeFlags&=~0x100u;
                if (definition->equipment.throwable && definition->equipment.repairable) { int extra=0; for(const auto &stat:values.stats) if(stat.effect=="item_extra_stack") extra+=stat.value; item.quantity=unsigned(std::clamp(int(definition->maxStack)+extra,1,511)); }
            }
            ++item.revision; draft.edit.changes.push_back({id,item.revision,partial?ItemChangeKind::DurabilityChanged:ItemChangeKind::PropertiesChanged,item.location,item.location,item.quantity}); total+=partial?record.gold:price;
        }
    }
    if (total>uint64_t(record.gold)+record.bankGold) return failure(DomainStatus::Conflict,12);
    const unsigned wallet=unsigned(std::min<uint64_t>(record.gold,total)); record.gold-=wallet; record.bankGold-=unsigned(total-wallet);
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),record.weaponSet};
    edit.equipment=rules; edit.character=record; edit.facts.emplace_back(MerchantFact{request.npc.id,receiptItem,operation,uint8_t(request.action==Action::Sell?1:request.action==Action::Repair || request.action==Action::RepairAll?2:0),record.gold});
    if (!draft.edit.spilled.empty()) {
        auto world=inventory::detail::prepareSpill(*player,draft.edit,*rules,ports_.items);
        if (!world) return failure(world.status);
        for (const auto &item:draft.edit.spilled) edit.publicFacts.emplace_back(GroundDropFact{world.value->next.world.items.at(item.id)});
        edit.world=std::move(*world.value);
    }
    auto bought=state_.bought;
    if(request.action==Action::Buy) bought.insert_or_assign(actor.player,receiptItem);
    auto plan=ports_.transactions.prepare(std::move(edit)); if (!plan) { if(isDeferred(plan.status)) return {plan.status,{}}; return failure(plan.status); }
    const auto result=ports_.transactions.commit(std::move(*plan.value)); if (!result) return {result.status,{}};
    if(itemIdentityCount) {
        ports_.items.publishIdentities(itemIdentityCursor,itemIdentityCount);
        ports_.random=itemRandom;
    }
    if(changedStocks) state_.stocks.swap(*changedStocks);
    state_.bought.swap(bought);
    if(prepared.pd2Random) ports_.pd2Random=*prepared.pd2Random;
    if (soldOffer) { auto &stock=request.gamble?state_.gambles.at({gambleOwner(actor.player,request.npc.id),request.npc.id}):state_.stocks.at(request.npc.id); stock.offers.erase(soldOffer); ++stock.revision; }
    state_.pending.erase(queued); return result;
}
}

namespace d2x::server::merchant {
StepStatus System::step(TickContext tick, FrameFacts &) {
    std::erase_if(state_.bought,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered;});
    const auto valid = [&](PlayerId id, EntityId npc, uint64_t revision) {
        const auto *conversation=ports_.npc.conversation(id);
        return conversation && conversation->npc==npc && conversation->revision==revision;
    };
    std::erase_if(state_.opened, [&](const auto &entry) { return !valid(entry.first, entry.second.npc, entry.second.conversation); });
    std::erase_if(state_.gambling,[&](PlayerId id){return !state_.opened.contains(id);});
    std::erase_if(state_.gambles,[&](const auto &entry){return !state_.sharedGheeds.contains(entry.first.second) && !valid(entry.first.first,entry.first.second,entry.second.conversation);});
    std::erase_if(state_.pending, [&](const auto &entry) { return !valid(entry.first, entry.second.request.npc.id,entry.second.conversation); });
    for(auto &[id,stock]:state_.stocks) {(void)id;if(stock.refreshAt && tick.tick>stock.refreshAt) {stock.refreshPending=true;stock.refreshAt=tick.tick+6000;}}
    // SUNITPROXY_UpdateVendorInventory clears a town's inventory once the
    // last living participant leaves. An observer still in town preserves it.
    std::erase_if(state_.stocks,[&](const auto &entry){return std::none_of(ports_.players.all().begin(),ports_.players.all().end(),[&](const auto &p){return p.second.entered && p.second.area==entry.second.area;});});
    return StepStatus::Complete;
}
}

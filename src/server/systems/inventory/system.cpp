#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/trade/system.hpp"
#include <stdexcept>

namespace d2x::server::inventory {
std::optional<InputState> System::input(PlayerId id) const {
    const auto *player = ports_.players.find(id);
    if (!player) return {};
    InputState result{player->persistent.containers, player->persistent.player.weaponSet, {}};
    result.trade=ports_.trade.container(id);
    for (const auto &[key, item] : player->persistent.inventory.items)
        if (const auto *location = std::get_if<ContainerLocation>(&item.location))
            result.items.emplace(key, InputItem{item.handle(), *location});
    return result;
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if (!supports(request)) return {};
    state_.pickups.erase(actor.player);
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || !area || area->generation != actor.areaGeneration) return {DomainStatus::InvalidActor, {}};
    if (!ports_.definitions || !player->rules.equipment || !player->rules.character) return {DomainStatus::Unavailable, {}};
    const auto trade=ports_.trade.container(actor.player);
    if (ports_.trade.find(actor.player)) {
        if (!trade || !ports_.trade.canEdit(actor.player) ||
            (!std::holds_alternative<MoveItem>(request.intent) && !std::holds_alternative<SwapItems>(request.intent)))
            return {DomainStatus::Conflict,{}};
        const auto allowed=[&](const ContainerLocation &at) { return at.container==trade || at.container==player->persistent.containers.backpack || at.container==player->persistent.containers.cursor; };
        const auto origin=[&](ItemHandle handle) {
            const auto item=player->persistent.inventory.items.find(handle.id);
            const auto *at=item==player->persistent.inventory.items.end()?nullptr:std::get_if<ContainerLocation>(&item->second.location);
            return at && allowed(*at);
        };
        if (const auto *move=std::get_if<MoveItem>(&request.intent)) {
            const auto *at=std::get_if<ContainerLocation>(&move->destination);
            if(!at || !allowed(*at) || !origin(move->item)) return {DomainStatus::InvalidRequest,{}};
        } else {
            const auto &swap=std::get<SwapItems>(request.intent);
            if(!origin(swap.first) || !origin(swap.second) || (swap.destination && !allowed(*swap.destination))) return {DomainStatus::InvalidRequest,{}};
        }
    }
    if(std::holds_alternative<CloseCube>(request.intent)) {
        const auto access=state_.storage.find(actor.player);
        if(access!=state_.storage.end() && access->second.kind==ContainerKind::Cube) state_.storage.erase(access);
        return {DomainStatus::Applied,std::monostate{}};
    }
    if (const auto *gold = std::get_if<GoldTransaction>(&request.intent); gold && gold->action == GoldAction::Drop) return dropGold(actor, gold->amount);
    if (std::holds_alternative<GoldTransaction>(request.intent) || std::holds_alternative<CloseStorage>(request.intent)) return storage(actor, request);
    if (const auto *identification = std::get_if<IdentifyItem>(&request.intent)) return identify(actor, *identification);
    if (const auto *transfer = std::get_if<GroundTransfer>(&request.intent)) {
        const auto result = ground(actor, *transfer);
        if (!transfer->drop && result.status == DomainStatus::Capacity && !ports_.transactions.hasOutputCapacity()) {
            state_.pickups.insert_or_assign(actor.player, Pickup{actor, *transfer, player->locomotionSequence,ports_.items.groundGeneration(transfer->item.id)});
            return {DomainStatus::Applied, std::monostate{}};
        }
        return result;
    }
    if (const auto *use = std::get_if<UseItem>(&request.intent)) return consume(actor, *use, request.source);
    DomainResult<Edit> planned;
    try { planned = plan(*player, request, *ports_.definitions, *player->rules.equipment, *player->rules.character, storageAccess(actor.player),cubeAccess(actor.player),trade); }
    catch (const std::runtime_error &) { return {DomainStatus::Unavailable, {}}; }
    catch (const std::out_of_range &) { return {DomainStatus::Unavailable, {}}; }
    if (!planned) return {planned.status, {}};
    auto &edit = *planned.value;
    transactions::InventoryEdit change{actor, player->inventoryRevision, player->characterRevision,
        std::move(edit.inventory), std::move(edit.changes), edit.weaponSet};
    if (!edit.spilled.empty()) {
        auto rules = std::make_shared<EquipmentRules>(*player->rules.equipment);
        auto world = detail::prepareSpill(*player, edit, *rules, ports_.items);
        if (!world) return {world.status, {}};
        for (const auto &item : edit.spilled) change.publicFacts.emplace_back(GroundDropFact{world.value->next.world.items.at(item.id)});
        change.world = std::move(*world.value); change.equipment = std::move(rules);
    }
    bool offerChanged=false;
    if(trade) {
        change.playerTrade=true;
        for(const auto &delta:change.changes) for(const auto *location:{&delta.before,&delta.after}) {
            const auto *at=*location?std::get_if<ContainerLocation>(&**location):nullptr;
            offerChanged=offerChanged || (at && at->container==trade);
        }
        if(offerChanged) {const auto decorated=ports_.trade.decorate(actor.player,change);if(!decorated) return decorated;}
    }
    auto transaction = ports_.transactions.prepare(std::move(change));
    if (!transaction) return {transaction.status, {}};
    const auto result=ports_.transactions.commit(std::move(*transaction.value));
    if(result && offerChanged) ports_.trade.edited(actor.player,actor.tick);
    return result;
}
DomainResult<> System::close(PlayerId player) {
    state_.storage.erase(player);
    state_.pickups.erase(player);
    return {DomainStatus::Applied,std::monostate{}};
}
}

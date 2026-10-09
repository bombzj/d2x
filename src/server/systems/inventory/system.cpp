#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/items/system.hpp"
#include <stdexcept>

namespace d2x::server::inventory {
std::optional<InputState> System::input(PlayerId id) const {
    const auto *player = ports_.players.find(id);
    if (!player) return {};
    InputState result{player->persistent.containers, player->persistent.player.weaponSet, {}};
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
    try { planned = plan(*player, request, *ports_.definitions, *player->rules.equipment, *player->rules.character, storageAccess(actor.player),cubeAccess(actor.player)); }
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
    auto transaction = ports_.transactions.prepare(std::move(change));
    if (!transaction) return {transaction.status, {}};
    return ports_.transactions.commit(std::move(*transaction.value));
}
DomainResult<> System::close(PlayerId player) {
    state_.storage.erase(player);
    state_.pickups.erase(player);
    return {DomainStatus::Applied,std::monostate{}};
}
}

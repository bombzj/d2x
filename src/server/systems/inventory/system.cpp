#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/systems/transactions/system.hpp"
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
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area) return {DomainStatus::InvalidActor, {}};
    if (!ports_.definitions || !player->rules.equipment || !player->rules.character) return {DomainStatus::Unavailable, {}};
    DomainResult<Edit> planned;
    try { planned = plan(*player, request, *ports_.definitions, *player->rules.equipment, *player->rules.character); }
    catch (const std::runtime_error &) { return {DomainStatus::Unavailable, {}}; }
    catch (const std::out_of_range &) { return {DomainStatus::Unavailable, {}}; }
    if (!planned) return {planned.status, {}};
    auto &edit = *planned.value;
    auto transaction = ports_.transactions.prepare(transactions::InventoryEdit{actor, player->inventoryRevision, player->characterRevision,
        std::move(edit.inventory), std::move(edit.changes), edit.weaponSet});
    if (!transaction) return {transaction.status, {}};
    return ports_.transactions.commit(std::move(*transaction.value));
}
DomainResult<> System::close(PlayerId) {
    return {};
}
}

#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/transactions/system.hpp"
#include <algorithm>
namespace d2x::server::inventory {
DomainResult<> System::consume(const ActorContext &actor, const UseItem &request, Source source) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->persistent.player.hp <= 0 || !player->rules.potions) return {DomainStatus::InvalidActor, {}};
    detail::Draft draft(*player, *ports_.definitions, *player->rules.equipment, *player->rules.character);
    const auto *item = draft.resolve(request.item);
    if (!item || item->revision == UINT64_MAX) return {DomainStatus::Stale, {}};
    const auto *location = std::get_if<ContainerLocation>(&item->location);
    const auto container = source == Source::Belt ? draft.containers().belt : draft.containers().backpack;
    if (!location || location->container != container || !draft.owned(*location)) return {DomainStatus::InvalidRequest, {}};
    if(player->rules.character->portalItems.contains(item->definition)) return ports_.travel.createPortal(actor,request.item);
    const auto definition = player->rules.potions->find(item->definition);
    if (definition == player->rules.potions->end()) return {DomainStatus::NotImplemented, {}};
    auto effects = ports_.effects.potion(actor, definition->second);
    if (!effects) return {effects.status, {}};
    const auto origin = *location;
    const auto id = item->id;
    auto &consumed = draft.edit.inventory.items.at(id);
    if (!consumed.quantity) return {DomainStatus::InvalidRequest, {}};
    --consumed.quantity; ++consumed.revision;
    const bool removed = consumed.quantity == 0;
    draft.edit.changes.push_back({id, consumed.revision, removed ? ItemChangeKind::Removed : ItemChangeKind::QuantityChanged,
        consumed.location, removed ? std::nullopt : std::optional<ItemLocation>{consumed.location}, consumed.quantity});
    if (removed) draft.edit.inventory.items.erase(id);
    if (removed && origin.container == draft.containers().belt) {
        const auto rows = draft.edit.inventory.containers.at(origin.container).spec.rows;
        int ready = 0;
        for (int row = 0; row < rows; ++row) {
            const auto above = draft.at({origin.container, {origin.cell.x, row}});
            if (!above) continue;
            if (row != ready) {
                const auto moved = draft.move(above, {origin.container, {origin.cell.x, ready}});
                if (moved != DomainStatus::Applied) return {moved, {}};
            }
            ++ready;
        }
    }
    transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), draft.edit.weaponSet};
    edit.character = effects.value->character; edit.transient = effects.value->transient;
    auto plan = ports_.transactions.prepare(std::move(edit));
    if (!plan) return {plan.status, {}};
    const auto result = ports_.transactions.commit(std::move(*plan.value));
    if (result) ports_.effects.commit(std::move(*effects.value));
    return result;
}
}

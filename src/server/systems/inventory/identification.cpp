#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/systems/transactions/system.hpp"

namespace d2x::server::inventory {
DomainResult<> System::identify(const ActorContext &actor, const IdentifyItem &request) {
    const auto &player = *ports_.players.find(actor.player);
    if (player.persistent.player.hp <= 0 || request.source.id == request.target.id) return {DomainStatus::InvalidRequest, {}};
    detail::Draft draft(player, *ports_.definitions, *player.rules.equipment, *player.rules.character);
    draft.storage = storageAccess(actor.player);
    const auto *source = draft.resolve(request.source), *target = draft.resolve(request.target);
    if (!source || !target) return {DomainStatus::Stale, {}};
    if (source->revision == UINT64_MAX || target->revision == UINT64_MAX) return {DomainStatus::Capacity, {}};
    const auto *from = std::get_if<ContainerLocation>(&source->location), *to = std::get_if<ContainerLocation>(&target->location);
    const auto &containers = draft.containers();
    if (!from || from->container != containers.backpack || !to || !draft.owned(*to) ||
        (to->container != containers.backpack && to->container != containers.equipment && to->container != containers.beltEquipment &&
         !(draft.storage && to->container == containers.stash)) || draft.at({containers.cursor, {}}) || target->identified)
        return {DomainStatus::InvalidRequest, {}};
    const auto rule = player.rules.character->identificationItems.find(source->definition);
    if (rule == player.rules.character->identificationItems.end() || !(rule->second ? source->charges : source->quantity))
        return {DomainStatus::InvalidRequest, {}};
    auto &identified = draft.edit.inventory.items.at(target->id);
    identified.identified = true; identified.nativeFlags |= 0x10; ++identified.revision;
    draft.edit.changes.push_back({identified.id, identified.revision, ItemChangeKind::PropertiesChanged,
        identified.location, identified.location, identified.quantity});
    auto &used = draft.edit.inventory.items.at(source->id); ++used.revision;
    const bool removed = rule->second ? (--used.charges, false) : --used.quantity == 0;
    draft.edit.changes.push_back({used.id, used.revision, removed ? ItemChangeKind::Removed : ItemChangeKind::QuantityChanged,
        used.location, removed ? std::nullopt : std::optional<ItemLocation>{used.location}, used.quantity});
    if (removed) draft.edit.inventory.items.erase(request.source.id);
    transactions::InventoryEdit edit{actor, player.inventoryRevision, player.characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), player.persistent.player.weaponSet};
    auto plan = ports_.transactions.prepare(std::move(edit));
    return plan ? ports_.transactions.commit(std::move(*plan.value)) : DomainResult<>{plan.status, {}};
}
}

#include "planning.hpp"
#include <type_traits>
#include <algorithm>

namespace d2x::server::inventory {
bool supports(const Request &request) {
    if(std::holds_alternative<CloseCube>(request.intent)) return true;
    return std::holds_alternative<IdentifyItem>(request.intent) || std::holds_alternative<CloseStorage>(request.intent) || std::holds_alternative<GoldTransaction>(request.intent) || std::holds_alternative<UseItem>(request.intent) || std::holds_alternative<GroundTransfer>(request.intent) || std::holds_alternative<MoveItem>(request.intent) || std::holds_alternative<EquipItem>(request.intent) ||
        std::holds_alternative<MergeStacks>(request.intent) || std::holds_alternative<LoadBook>(request.intent) ||
        std::holds_alternative<SwapItems>(request.intent) || std::holds_alternative<SwitchWeaponSet>(request.intent);
}
DomainResult<Edit> plan(const PlayerState &player, const Request &request, const ItemCatalog &catalog, const EquipmentRules &rules, const CharacterRules &characterRules, bool storage, bool cube) {
    if (!supports(request)) return {};
    if (player.persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    if (request.weaponSet && *request.weaponSet != player.persistent.player.weaponSet) return {DomainStatus::Stale, {}};
    detail::Draft draft(player, catalog, rules, characterRules); draft.storage=storage;
    const auto cubeCarried=std::any_of(player.persistent.inventory.items.begin(),player.persistent.inventory.items.end(),[&](const auto &entry) {
        const auto *at=std::get_if<ContainerLocation>(&entry.second.location); const auto *def=catalog.find(entry.second.definition);
        return at && at->container==player.persistent.containers.backpack && def && def->opensCube;
    });
    const auto stored = [&](EntityId id) { return id==player.persistent.containers.backpack || (cube && cubeCarried && id==player.persistent.containers.cube) || (storage && id==player.persistent.containers.stash); };
    for (const auto guard : request.equipmentGuards)
        if (!draft.resolve(guard)) return {DomainStatus::Stale, {}};
    const auto &containers = draft.containers();
    const auto cursor = ContainerLocation{containers.cursor, {}};
    const auto result = std::visit([&]<class T>(const T &operation) -> DomainStatus {
        if constexpr (std::is_same_v<T, MoveItem>) {
            const auto *item = draft.resolve(operation.item);
            if (!item) return DomainStatus::Stale;
            const auto *origin = std::get_if<ContainerLocation>(&item->location);
            const auto *target = std::get_if<ContainerLocation>(&operation.destination);
            if (!origin || !target || !draft.owned(*origin)) return DomainStatus::InvalidRequest;
            const bool taking = request.source == Source::Stored || request.source == Source::Belt;
            if (taking) {
                if (*target != cursor || (request.source == Source::Belt ? origin->container!=containers.belt : !stored(origin->container)))
                    return DomainStatus::InvalidRequest;
            } else if (*origin != cursor || (!stored(target->container) && target->container != containers.belt))
                return DomainStatus::InvalidRequest;
            if (!draft.fits(item->id, *target)) return DomainStatus::Conflict;
            return draft.move(item->id, *target);
        } else if constexpr (std::is_same_v<T, SwapItems>) {
            const auto *first = draft.resolve(operation.first), *second = draft.resolve(operation.second);
            if (!first || !second) return DomainStatus::Stale;
            const auto *origin = std::get_if<ContainerLocation>(&second->location);
            if (first->id == second->id || first->location != ItemLocation{cursor} || !origin || !draft.owned(*origin) ||
                (request.source == Source::Belt ? origin->container!=containers.belt : !stored(origin->container))) return DomainStatus::InvalidRequest;
            const auto target = operation.destination.value_or(*origin);
            if (target.container != origin->container || !draft.fits(first->id, target, second->id) ||
                !draft.fits(second->id, cursor, first->id)) return DomainStatus::Conflict;
            auto status = draft.move(first->id, target);
            return status == DomainStatus::Applied ? draft.move(second->id, cursor) : status;
        } else if constexpr (std::is_same_v<T, MergeStacks>) {
            return draft.merge(operation);
        } else if constexpr (std::is_same_v<T, LoadBook>) {
            return draft.loadBook(operation);
        } else if constexpr (std::is_same_v<T, EquipItem>) {
            return draft.equipment(operation, request.equipmentMode);
        } else if constexpr (std::is_same_v<T, SwitchWeaponSet>) {
            if (draft.at(cursor)) return DomainStatus::Conflict;
            draft.edit.weaponSet ^= 1;
            // Preserve existing equipment, including an unusable loadout. Switching
            // does not equip an item or require its stats to be enabled.
            return DomainStatus::Applied;
        } else return DomainStatus::NotImplemented;
    }, request.intent);
    if (result != DomainStatus::Applied) return {result, {}};
    return {DomainStatus::Applied, std::move(draft.edit)};
}
}

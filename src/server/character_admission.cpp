#include "character_admission.hpp"
#include <functional>
#include <stdexcept>

namespace d2x::server {
PersistentCharacter admitCharacter(PersistentCharacter state, uint64_t nextEntity) {
    EntityIds ids(nextEntity);
    std::map<EntityId, EntityId> mapping;
    auto allocate = [&](EntityId old) {
        if (!old || !mapping.emplace(old, ids.allocate()).second) throw std::runtime_error("Duplicate character entity identity");
    };
    allocate(state.player.id);
    for (const auto &[id, c] : state.inventory.containers) allocate(id);
    for (const auto &corpse : state.corpses) allocate(corpse.id);
    std::function<void(const ItemInstance &)> allocateItem = [&](const ItemInstance &item) {
        allocate(item.id);
        for (const auto &child : item.socketedItems) allocateItem(child);
    };
    for (const auto &[id, item] : state.inventory.items) allocateItem(item);
    if (state.ironGolem) allocateItem(*state.ironGolem);
    auto remap = [&](EntityId &id) { if (id) id = mapping.at(id); };
    remap(state.player.id);
    for (auto *id : {&state.containers.backpack, &state.containers.belt, &state.containers.stash,
        &state.containers.beltEquipment, &state.containers.equipment, &state.containers.cube,
        &state.containers.hirelingEquipment, &state.containers.cursor}) remap(*id);
    std::map<EntityId, ContainerState> containers;
    for (auto &[id, c] : state.inventory.containers) {
        remap(c.id); remap(c.spec.owner); containers.emplace(c.id, std::move(c));
    }
    state.inventory.containers = std::move(containers);
    std::function<void(ItemInstance &, bool)> remapItem = [&](ItemInstance &item, bool golem) {
        remap(item.id);
        if (auto *at = std::get_if<ContainerLocation>(&item.location)) {
            if (!golem) remap(at->container); else at->container = {};
        } else if (auto *socket = std::get_if<SocketLocation>(&item.location)) remap(socket->host);
        for (auto &child : item.socketedItems) remapItem(child, false);
    };
    std::map<EntityId, ItemInstance> items;
    for (auto &[id, item] : state.inventory.items) { remapItem(item, false); items.emplace(item.id, std::move(item)); }
    state.inventory.items = std::move(items);
    if (state.ironGolem) remapItem(*state.ironGolem, true);
    for (auto &corpse : state.corpses) { remap(corpse.id); remap(corpse.owner); remap(corpse.items); corpse.recoverableExperience = 0; }
    if (ids.cursor() > UINT32_MAX) throw std::runtime_error("Native entity ID capacity exhausted");
    state.nextEntityId = ids.cursor();
    return state;
}
} // namespace d2x::server

#include "gameplay/character/runtime_record.hpp"
#include "gameplay/session/session_impl.hpp"
#include "content/items/equipment_modifiers.hpp"
#include <algorithm>
#include <stdexcept>
#include <variant>

namespace d2x {
namespace {
void requireSave(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(std::string("Invalid save: ") + reason);
}
} // namespace

CharacterSaveData GameSessionImpl::characterSave() const {
    CharacterSaveData result;
    result.player = captureCharacterRecord(state().player);
    result.mapSeed = state().mapSeed;
    result.difficulty = state().population.difficulty;
    result.lastRegion = state().area.region;
    result.waypoints = state().waypoints;
    result.containers = playerContainers_;
    for (auto id : {playerContainers_.backpack, playerContainers_.belt, playerContainers_.stash,
                    playerContainers_.beltEquipment, playerContainers_.equipment,
                    playerContainers_.cube, playerContainers_.hirelingEquipment, playerContainers_.cursor})
        if (id) result.inventory.containers.emplace(id, inventory_.state().containers.at(id));
    for (const auto &[id, item] : inventory_.state().items)
        if (std::holds_alternative<ContainerLocation>(item.location))
            result.inventory.items.emplace(id, item);
    const auto &player = result.player;
    const auto base = deriveCharacterAttributes(characterDefinition_, player.level, player.allocated);
    const EquipmentActor actor{characterDefinition_.code, base.strength, base.dexterity,
                               player.level, base.blockFactor, player.weaponSet};
    const auto modifiers = resolveEquipmentModifiers(content_, inventory_, playerContainers_, actor);
    const auto limits = deriveCharacterAttributes(characterDefinition_, player.level, player.allocated, modifiers);
    result.player.hp = std::min(player.hp, float(limits.maxLife));
    result.player.mana = std::min(player.mana, float(limits.maxMana));
    result.player.stamina = std::min(player.stamina, float(limits.maxStamina));
    inventory_.validateSnapshot(result.inventory, result.containers, player.id);
    validateItemProperties(result);
    return result;
}

CharacterSaveData GameSessionImpl::prepareCharacterRestore(CharacterSaveData character) const {
    requireSave(!character.player.nativeSaveSections.empty(), "restore requires decoded D2S character data");
    {
        uint64_t next = ids_.cursor();
        std::map<EntityId, EntityId> containers;
        auto remap = [&](EntityId &id) {
            if (id) {
                EntityId replacement{next++};
                containers.emplace(id, replacement);
                id = replacement;
            }
        };
        remap(character.containers.backpack);
        remap(character.containers.belt);
        remap(character.containers.stash);
        remap(character.containers.beltEquipment);
        remap(character.containers.equipment);
        remap(character.containers.cube);
        remap(character.containers.hirelingEquipment);
        remap(character.containers.cursor);
        InventoryState inventory;
        for (const auto &[id, container] : character.inventory.containers) {
            auto copy = container;
            copy.id = containers.at(id);
            copy.spec.owner = state().player.id;
            inventory.containers.emplace(copy.id, copy);
        }
        for (auto &[id, item] : character.inventory.items) {
            auto &location = std::get<ContainerLocation>(item.location);
            location.container = containers.at(location.container);
            item.id = EntityId{next++};
            inventory.items.emplace(item.id, std::move(item));
        }
        character.player.id = state().player.id;
        character.nextEntityId = next;
        character.inventory = std::move(inventory);
    }
    requireSave(std::all_of(character.inventory.items.begin(), character.inventory.items.end(),
                            [](const auto &entry) {
                                return std::holds_alternative<ContainerLocation>(entry.second.location);
                            }), "ground item in character save");

    auto lastRegion = std::find_if(regions_.begin(), regions_.end(),
        [&](const Region &region) { return region.definition.id == character.lastRegion; });
    requireSave(lastRegion != regions_.end(), "last visited region");
    auto level = worldContent_.levels().find(int(lastRegion->definition.id));
    int act = level == worldContent_.levels().end() ? 0 : level->second.act;
    auto town = std::find_if(regions_.begin(), regions_.end(), [&](const Region &region) {
        auto record = worldContent_.levels().find(int(region.definition.id));
        return region.definition.safe && record != worldContent_.levels().end() &&
               record->second.act == act;
    });
    requireSave(town != regions_.end(), "act town is unavailable");

    character.player.hp = std::max(1.f, character.player.hp);
    character.lastRegion = town->definition.id;
    return character;
}
} // namespace d2x

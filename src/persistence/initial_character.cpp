#include "initial_character.hpp"
#include "content/classic_data.hpp"
#include "d2s_inventory.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
bool validCharacterName(std::string_view name) {
    return !name.empty() && name.size() <= 15 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}
PersistentCharacter createCharacter(const ClassicData &content, std::string name, unsigned index, uint32_t seed, bool hardcore) {
    if (!validCharacterName(name) || index >= content.characters.size())
        throw std::runtime_error("Invalid character name or class");
    PersistentCharacter result;
    const auto &definition = content.characters[index];
    result.player.name = std::move(name);
    result.player.hardcore = hardcore;
    result.player.characterClass = definition.name;
    result.mapSeed = seed;
    const auto attributes = deriveCharacterAttributes(definition, 1, {});
    result.player.hp = attributes.maxLifeFixed;
    result.player.mana = attributes.maxManaFixed;
    result.player.stamina = attributes.maxStaminaFixed;
    initializeD2sInventory(result, content);
    auto &inventory = result.inventory;
    inventory.creationRandom = initialRandom(seed);
    const auto &levels = content.tables.at("levels");
    bool waypointFound = false;
    for (size_t row = 0; row < levels.rows().size(); ++row)
        if (levels.number(row, "Waypoint") == 0) {
            const auto id = levels.number(row, "Id");
            if (!id) throw std::runtime_error("Initial waypoint lacks a level ID");
            result.lastRegion = RegionId(*id);
            result.waypoints.emplace(result.lastRegion, -1.f);
            waypointFound = true;
        }
    if (!waypointFound) throw std::runtime_error("MPQ lacks the initial waypoint");
    auto backpackCell = [&](const ItemDefinition &item) -> Cell {
        const auto &grid = inventory.containers.at(result.containers.backpack).spec;
        for (int y = 0; y <= grid.rows - item.height; ++y)
            for (int x = 0; x <= grid.columns - item.width; ++x) {
                bool occupied = false;
                for (const auto &[id, other] : inventory.items) {
                    const auto *at = std::get_if<ContainerLocation>(&other.location);
                    if (!at || at->container != result.containers.backpack) continue;
                    const auto *size = content.items.find(other.definition);
                    if (!size) throw std::runtime_error("Starter item definition disappeared");
                    occupied |= x < at->cell.x + size->width && x + item.width > at->cell.x &&
                                y < at->cell.y + size->height && y + item.height > at->cell.y;
                }
                if (!occupied) return {x, y};
            }
        throw std::runtime_error("Original starter inventory does not fit");
    };
    const auto &characters = content.tables.at("charstats");
    const auto *tree = content.skills.tree(definition.code);
    int beltColumn = 0;
    // Ported from master's createStarterEquipment / InventoryService::createItem:
    // normal items, original base durability, seeded graphic/defense, StartSkill.
    for (int slot = 1; slot <= 10; ++slot) {
        const auto field = "item" + std::to_string(slot);
        const auto code = characters.value(definition.sourceRow, field);
        const auto body = characters.value(definition.sourceRow, field + "loc");
        const int count = characters.number(definition.sourceRow, field + "count").value_or(0);
        if (code.empty() || code == "0") {
            if (count) throw std::runtime_error("Starter quantity without an item");
            continue;
        }
        const auto *base = content.items.find(code);
        if (!base || count <= 0) throw std::runtime_error("Invalid original starter item");
        const auto bodySlot = equipmentSlotFromCode(body);
        if (!body.empty() && !bodySlot) throw std::runtime_error("Unknown starter body slot");
        for (int n = 0; n < (body.empty() ? count : 1); ++n) {
            ItemInstance item;
            if(content.pd2Host) item.nativeFormat=104;
            item.id = EntityId{result.nextEntityId++};
            item.definition = code;
            item.quantity = body.empty() ? 1u : base->maxStack > 1 ? base->maxStack : unsigned(count);
            if (item.quantity > base->maxStack) throw std::runtime_error("Invalid starter stack");
            item.charges = base->bookInitialCharges;
            item.durability = base->maxDurability;
            item.nativeSeed = rollRandom(inventory.creationRandom);
            auto random = initialRandom(item.nativeSeed);
            if (!base->inventoryIcons.empty()) {
                rollRandom(random);
                item.nativeHasGraphic = true;
                item.nativeGraphic = uint32_t(random) % uint32_t(base->inventoryIcons.size());
            }
            if (base->family == ItemFamily::Armor) {
                if (!base->base.minDefense || !base->base.maxDefense || *base->base.minDefense < 0 ||
                    *base->base.maxDefense < *base->base.minDefense)
                    throw std::runtime_error("Missing starter armor defense");
                rollRandom(random);
                item.defense = *base->base.minDefense + int(uint32_t(random) % uint32_t(*base->base.maxDefense - *base->base.minDefense + 1));
            }
            if (bodySlot) {
                if (*bodySlot != EquipmentSlot::Belt && !base->equipment.fits(*bodySlot))
                    throw std::runtime_error("Starter item cannot occupy its original slot");
                item.location = ContainerLocation{*bodySlot == EquipmentSlot::Belt ? result.containers.beltEquipment : result.containers.equipment,
                    {*bodySlot == EquipmentSlot::Belt ? 0 : int(*bodySlot), 0}};
                if (*bodySlot == EquipmentSlot::Belt) {
                    if (base->beltRows <= 0) throw std::runtime_error("Invalid starter belt");
                    inventory.containers.at(result.containers.belt).spec.rows = base->beltRows;
                }
                if (slot == 1 && tree && tree->starterSkill)
                    item.grantedSkill = *tree->starterSkill;
            } else if (base->beltAllowed && beltColumn < inventory.containers.at(result.containers.belt).spec.columns)
                item.location = ContainerLocation{result.containers.belt, {beltColumn++, 0}};
            else item.location = ContainerLocation{result.containers.backpack, backpackCell(*base)};
            inventory.itemOrder.push_back(item.id);
            inventory.items.emplace(item.id, std::move(item));
        }
    }
    // PlrSave2's NEWBIE_SAVE branch selects CharStats.StartSkill on the right
    // after starter items have made it available. It is an equipment bonus,
    // not a learned rank or a charged skill owned by the starter item GUID.
    if (tree && tree->starterSkill && *tree->starterSkill > 0) {
        const bool available = std::any_of(inventory.items.begin(), inventory.items.end(), [&](const auto &entry) {
            const auto &item = entry.second;
            const auto *at = std::get_if<ContainerLocation>(&item.location);
            return item.grantedSkill == *tree->starterSkill && at && at->container == result.containers.equipment &&
                weaponSlotActive(EquipmentSlot(at->cell.x), result.player.weaponSet);
        });
        if (available) result.player.selectedSkills[result.player.weaponSet * 2 + 1] = *tree->starterSkill;
    }
    return result;
}

} // namespace d2x

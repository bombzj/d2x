#include "planning.hpp"
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/items/equipment_requirements.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::inventory::detail {
namespace {
bool hand(EquipmentSlot slot) {
    return slot == EquipmentSlot::RightHand || slot == EquipmentSlot::LeftHand ||
        slot == EquipmentSlot::AlternateRightHand || slot == EquipmentSlot::AlternateLeftHand;
}
bool both(const ItemDefinition &def, std::string_view characterClass) {
    return def.equipment.twoHanded && !(characterClass == "bar" && def.equipment.oneOrTwoHanded);
}
bool compatible(const ItemDefinition &a, const ItemDefinition &b, std::string_view characterClass) {
    const auto &left = a.equipment, &right = b.equipment;
    if (!left.shoots.empty() || !right.shoots.empty())
        return (!left.shoots.empty() && right.isType(left.shoots) && !right.quiver.empty() && left.isType(right.quiver)) ||
            (!right.shoots.empty() && left.isType(right.shoots) && !left.quiver.empty() && right.isType(left.quiver));
    if (!left.quiver.empty() || !right.quiver.empty() || both(a, characterClass) || both(b, characterClass)) return false;
    if (left.isType("shld") || right.isType("shld")) return left.isType("shld") ? right.isType("weap") : left.isType("weap");
    return left.isType("weap") && right.isType("weap") &&
        (characterClass == "bar" || (characterClass == "ass" && left.isType("h2h") && right.isType("h2h")));
}
}
DomainStatus Draft::qualified(EntityId candidate) const {
    auto state = player.persistent;
    state.inventory = edit.inventory;
    state.player.weaponSet = edit.weaponSet;
    const auto totals = attributes::calculate(player.definition, state, catalog, rules, characterRules, candidate, player.transient.modifiers);
    const auto view = attributes::loadout(state, catalog, rules);
    const auto &item = state.inventory.items.at(candidate);
    const auto *definition = catalog.find(item.definition);
    if (!definition) return DomainStatus::Unavailable;
    const auto &value = totals.character;
    const EquipmentActor actor{player.definition.code, value.strength, value.dexterity,
        state.player.level, value.blockFactor, state.player.weaponSet};
    const auto result = view.requirements({&item, definition}, actor);
    return result == InventoryError::None ? DomainStatus::Applied : DomainStatus::InvalidRequest;
}
DomainStatus Draft::equipment(const EquipItem &command, EquipmentMode mode) {
    const auto *source = resolve(command.item);
    if (!source) return DomainStatus::Stale;
    const auto *origin = std::get_if<ContainerLocation>(&source->location);
    if (!origin || !owned(*origin)) return DomainStatus::InvalidRequest;
    const auto *def = catalog.find(source->definition);
    if (!def) return DomainStatus::Unavailable;
    const auto cursor = ContainerLocation{containers().cursor, {}};
    if (!command.slot) {
        const auto *destination = command.destination ? std::get_if<ContainerLocation>(&*command.destination) : nullptr;
        if (!destination || *destination != cursor ||
            (origin->container != containers().equipment && origin->container != containers().beltEquipment) || !fits(source->id, cursor))
            return DomainStatus::InvalidRequest;
        const bool belt = origin->container == containers().beltEquipment;
        const auto result = move(source->id, cursor);
        if (result != DomainStatus::Applied) return result;
        return belt ? resizeBelt(1) : result;
    }
    const auto slot = *command.slot;
    if (command.destination || origin->container != containers().cursor || !source->identified ||
        !def->equipment.fits(slot) || !weaponSlotActive(slot, edit.weaponSet)) return DomainStatus::InvalidRequest;
    const bool belt = slot == EquipmentSlot::Belt;
    if (belt && (def->beltRows < 1 || def->beltRows > 4)) return DomainStatus::Unavailable;
    const EntityId previous = equipped(slot);
    EntityId opposite;
    if (hand(slot)) opposite = equipped(weaponHandSlot(slot == weaponHandSlot(false, edit.weaponSet), edit.weaponSet));
    const auto *oppositeDefinition = opposite ? catalog.find(edit.inventory.items.at(opposite).definition) : nullptr;
    if (opposite && !oppositeDefinition) return DomainStatus::Unavailable;
    const bool conflicts = opposite && !compatible(*def, *oppositeDefinition, player.definition.code);
    switch (mode) {
    case EquipmentMode::Insert: if (previous || conflicts) return DomainStatus::Conflict; break;
    case EquipmentMode::Swap: if (!previous || conflicts) return DomainStatus::Conflict; break;
    case EquipmentMode::Indirect: if (!hand(slot) || previous || !conflicts) return DomainStatus::Conflict; break;
    case EquipmentMode::TwoHanded:
        if (!hand(slot) || !previous || !opposite || !both(*def, player.definition.code)) return DomainStatus::Conflict;
        break;
    }
    const auto destination = belt ? ContainerLocation{containers().beltEquipment, {}}
        : ContainerLocation{containers().equipment, {int(slot), 0}};
    auto result = move(source->id, destination);
    if (result != DomainStatus::Applied) return result;
    if (previous) {
        result = move(previous, cursor);
        if (result != DomainStatus::Applied) return result;
    }
    if (conflicts) {
        const auto target = previous ? space(opposite, containers().backpack) : std::optional<ContainerLocation>{cursor};
        if (!target) return DomainStatus::Capacity;
        result = move(opposite, *target);
        if (result != DomainStatus::Applied) return result;
    }
    result = qualified(source->id);
    if (result != DomainStatus::Applied) return result;
    return belt ? resizeBelt(def->beltRows) : result;
}
}

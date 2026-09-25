#include "gameplay/session/session.hpp"
#include "content/equipment_modifiers.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
namespace {
CharacterModifiers activeModifiers(const PlayerState &player, float now) {
    CharacterModifiers result;
    for (const auto &effect : player.combatEffects)
        if (effect.expiresAt > now)
            mergeCharacterModifiers(result, effect.modifiers);
    return result;
}
}
EquipmentActor GameSession::equipmentActor() const {
    return equipmentActor(state().player);
}
EquipmentActor GameSession::equipmentActor(const PlayerState &player) const {
    auto effects = activeModifiers(player, state().time);
    auto base = deriveCharacterAttributes(characterDefinition_, player.level, player.allocated, effects);
    EquipmentActor baseActor{characterDefinition_.code, base.strength, base.dexterity, player.level,
                             base.blockFactor, player.weaponSet};
    auto modifiers = resolveEquipmentModifiers(content_, inventory_, playerContainers_, baseActor);
    mergeCharacterModifiers(modifiers, effects);
    auto stats = deriveCharacterAttributes(characterDefinition_, player.level, player.allocated, modifiers);
    return {characterDefinition_.code, stats.strength, stats.dexterity, player.level,
            stats.blockFactor, player.weaponSet};
}
void GameSession::refreshCharacter(bool fillGains) {
    auto &player = simulation_.state_.player;
    const auto previous = simulation_.characterStats_;
    auto effects = activeModifiers(player, state().time);
    auto base = deriveCharacterAttributes(characterDefinition_, player.level, player.allocated, effects);
    EquipmentActor baseActor{characterDefinition_.code, base.strength, base.dexterity, player.level,
                             base.blockFactor, player.weaponSet};
    auto modifiers = resolveEquipmentModifiers(content_, inventory_, playerContainers_, baseActor);
    mergeCharacterModifiers(modifiers, effects);
    auto current = deriveCharacterAttributes(characterDefinition_, player.level, player.allocated,
                                             modifiers, simulation_.resistancePenalty_);
    if (fillGains) {
        if (player.hp > 0) player.hp += current.maxLife - previous.maxLife;
        player.mana += current.maxMana - previous.maxMana;
        player.stamina += current.maxStamina - previous.maxStamina;
    }
    player.hp = std::clamp(player.hp, 0.f, float(current.maxLife));
    player.mana = std::clamp(player.mana, 0.f, float(current.maxMana));
    player.stamina = std::clamp(player.stamina, 0.f, float(current.maxStamina));
    simulation_.characterStats_ = current;
    EquipmentActor actor{characterDefinition_.code, current.strength, current.dexterity, player.level,
                         current.blockFactor, player.weaponSet};
    simulation_.equipmentStats_ = deriveEquipmentStats(inventory_, playerContainers_, actor,
                                                       modifiers.defense, modifiers.combat);
}
void GameSession::createStarterEquipment() {
    const bool legacy = content_.profile == "classic-1.04-txt-v1";
    const auto &characters = content_.tables.at("charstats");
    const auto actor = equipmentActor();
    InventoryAccess access;
    access.actor = state().player.id;
    int beltColumn = 0;
    const size_t row = characterDefinition_.sourceRow;
    for (int index = 1; index <= 10; ++index) {
        auto field = "item" + std::to_string(index);
        auto body = characters.value(row, field + "loc");
        auto code = characters.value(row, field);
        int quantity = characters.number(row, field + "count").value_or(0);
        if (code.empty() || code == "0") {
            if (quantity)
                throw std::runtime_error("Starter item count without definition");
            continue;
        }
        if (quantity <= 0)
            throw std::runtime_error("Invalid starter equipment quantity");
        if (body.empty() || (legacy && body == "0")) {
            const auto *definition = inventory_.catalog().find(code);
            if (!definition)
                throw std::runtime_error("Unknown original starter item: " + std::string(code));
            for (int count = 0; count < quantity; ++count) {
                ItemDestination destination = AutoPlace{playerContainers_.backpack};
                auto belt = inventory_.container(playerContainers_.belt);
                if (definition->beltAllowed && belt && beltColumn < belt->spec.columns)
                    destination = ContainerLocation{playerContainers_.belt, {beltColumn++, 0}};
                if (!inventory_.createItem(code, 1, destination))
                    throw std::runtime_error("Cannot create original starter item: " + std::string(code));
            }
            continue;
        }
        if (legacy)
            continue; // Numeric 1.04 equipment locations have no verified slot adapter.
        auto bodySlot = equipmentSlotFromCode(body);
        if (!bodySlot)
            throw std::runtime_error("Unsupported starter body location");
        const auto *definition = inventory_.catalog().find(code);
        if (!definition) throw std::runtime_error("Unknown original starter equipment: " + std::string(code));
        auto created = inventory_.createItem(code,
            definition->maxStack > 1 ? definition->maxStack : unsigned(quantity),
            AutoPlace{playerContainers_.backpack});
        if (!created)
            throw std::runtime_error("Cannot create original starter equipment: " + std::string(code));
        if (index == 1)
            if (const auto *tree = content_.skills.tree(characterDefinition_.code);
                tree && tree->starterSkill)
                inventory_.state_.items.at(created.item).grantedSkill = *tree->starterSkill;
        auto handle = inventory_.item(created.item)->handle();
        auto slot = *bodySlot;
        auto equipped = slot == EquipmentSlot::Belt
                            ? inventory_.equipBelt(EquipBelt{handle}, playerContainers_, access, actor)
                            : inventory_.equip(EquipItem{handle, slot}, playerContainers_, access, actor);
        if (!equipped)
            throw std::runtime_error("Cannot equip original starter item: " + std::string(code));
    }
}
bool GameSession::inventorySourceAllowed(EntityId id) const {
    const auto *item = inventory_.item(id);
    if (item)
        if (auto ground = std::get_if<GroundLocation>(&item->location))
            return inventoryDestinationAllowed(*ground);
    return true; // Missing IDs and stale revisions are reported by InventoryService.
}
InventoryError GameSession::previewInventory(const GameCommand &command) const {
    return std::visit(
        [&](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SplitStack>) {
                EntityId source;
                if constexpr (std::is_same_v<T, MoveItem>)
                    source = intent.item.id;
                else
                    source = intent.source.id;
                if (!inventorySourceAllowed(source))
                    return InventoryError::AccessDenied;
                if constexpr (std::is_same_v<T, MoveItem>) {
                    const auto *item = inventory_.item(source);
                    if (item && inventory_.catalog().find(item->definition)->opensCube &&
                        std::holds_alternative<GroundLocation>(intent.destination) &&
                        !inventory_.contents(playerContainers_.cube).empty())
                        return InventoryError::RestrictedItem;
                }
                if (!inventoryDestinationAllowed(intent.destination))
                    return InventoryError::InvalidLocation;
                return inventory_.preview(intent, inventoryAccess());
            } else if constexpr (std::is_same_v<T, SwapItems> || std::is_same_v<T, MergeStacks> ||
                                 std::is_same_v<T, LoadBook>) {
                EntityId first, second;
                if constexpr (std::is_same_v<T, SwapItems>) {
                    first = intent.first.id;
                    second = intent.second.id;
                } else if constexpr (std::is_same_v<T, MergeStacks>) {
                    first = intent.source.id;
                    second = intent.target.id;
                } else {
                    first = intent.scroll.id;
                    second = intent.book.id;
                }
                if (!inventorySourceAllowed(first) || !inventorySourceAllowed(second))
                    return InventoryError::AccessDenied;
                return inventory_.preview(intent, inventoryAccess());
            } else if constexpr (std::is_same_v<T, TransferItem>) {
                if (!inventorySourceAllowed(intent.item.id))
                    return InventoryError::AccessDenied;
                return inventory_.preview(intent, inventoryAccess());
            } else if constexpr (std::is_same_v<T, EquipItem> || std::is_same_v<T, EquipBelt>) {
                if (intent.destination && !inventoryDestinationAllowed(*intent.destination))
                    return InventoryError::InvalidLocation;
                return inventory_.preview(intent, playerContainers_, inventoryAccess(), equipmentActor());
            } else if constexpr (std::is_same_v<T, UseItem>) {
                const auto *source = inventory_.item(intent.item.id);
                if (source && (content_.isPortalScroll(source->definition) ||
                               content_.isPortalScroll(
                                   inventory_.catalog().find(source->definition)->bookScroll)))
                    return previewPortalScroll(intent.item);
                auto error = inventory_.previewDrink(intent.item, inventoryAccess());
                if (error != InventoryError::None)
                    return error;
                return content_.potion(inventory_.item(intent.item.id)->definition)
                           ? InventoryError::None
                           : InventoryError::UnsupportedUse;
            } else if constexpr (std::is_same_v<T, IdentifyItem>) {
                if (intent.source.id == intent.target.id)
                    return InventoryError::InvalidRequest;
                if (auto error = inventory_.checkHandle(intent.source); error != InventoryError::None)
                    return error;
                if (auto error = inventory_.checkHandle(intent.target); error != InventoryError::None)
                    return error;
                const auto &source = *inventory_.item(intent.source.id);
                const auto &target = *inventory_.item(intent.target.id);
                const auto *definition = inventory_.catalog().find(source.definition);
                if (!content_.isIdentifyScroll(source.definition) &&
                    !content_.isIdentifyScroll(definition->bookScroll))
                    return InventoryError::UnsupportedUse;
                if (!definition->bookScroll.empty() && !source.charges)
                    return InventoryError::InvalidQuantity;
                auto origin = std::get_if<ContainerLocation>(&source.location);
                if (!origin || (origin->container != playerContainers_.backpack &&
                                origin->container != playerContainers_.belt))
                    return InventoryError::AccessDenied;
                auto destination = std::get_if<ContainerLocation>(&target.location);
                if (!destination || (destination->container != playerContainers_.backpack &&
                                     destination->container != playerContainers_.equipment &&
                                     destination->container != playerContainers_.beltEquipment &&
                                     destination->container != playerContainers_.stash))
                    return InventoryError::AccessDenied;
                if (destination->container == playerContainers_.stash &&
                    storage().container != playerContainers_.stash)
                    return InventoryError::AccessDenied;
                return target.identified ? InventoryError::InvalidRequest : InventoryError::None;
            } else
                return InventoryError::InvalidRequest;
        },
        command);
}
void GameSession::executeInventory(const GameCommand &command) {
    std::visit(
        [&](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SwapItems> ||
                          std::is_same_v<T, SplitStack> || std::is_same_v<T, MergeStacks> ||
                          std::is_same_v<T, LoadBook> ||
                          std::is_same_v<T, EquipBelt> || std::is_same_v<T, TransferItem> ||
                          std::is_same_v<T, EquipItem>) {
                EntityId requested;
                if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, EquipBelt> ||
                              std::is_same_v<T, TransferItem> || std::is_same_v<T, EquipItem>)
                    requested = intent.item.id;
                else if constexpr (std::is_same_v<T, SwapItems>)
                    requested = intent.first.id;
                else if constexpr (std::is_same_v<T, LoadBook>)
                    requested = intent.scroll.id;
                else
                    requested = intent.source.id;
                auto error = previewInventory(command);
                if (error != InventoryError::None) {
                    simulation_.emit(InventoryRejected{requested, error});
                    return;
                }
                if constexpr (std::is_same_v<T, MoveItem>)
                    publishInventory(inventory_.move(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, SwapItems>)
                    publishInventory(inventory_.swap(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, SplitStack>)
                    publishInventory(inventory_.split(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, TransferItem>)
                    publishInventory(inventory_.transfer(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, EquipItem>)
                    publishInventory(
                        inventory_.equip(intent, playerContainers_, inventoryAccess(), equipmentActor()),
                        requested);
                else if constexpr (std::is_same_v<T, EquipBelt>) {
                    auto result =
                        inventory_.equipBelt(intent, playerContainers_, inventoryAccess(), equipmentActor());
                    bool applied = bool(result);
                    publishInventory(std::move(result), requested);
                    if (applied)
                        simulation_.emit(BeltEquipped{});
                } else if constexpr (std::is_same_v<T, LoadBook>)
                    publishInventory(inventory_.loadBook(intent, inventoryAccess()), requested);
                else
                    publishInventory(inventory_.merge(intent, inventoryAccess()), requested);
            }
        },
        command);
}
std::optional<GroundLocation> GameSession::dropLocation() const {
    const auto &player = state().player;
    if (player.dead)
        return std::nullopt;
    Vec position = map().grid.nearest(player.pos + player.look.unit());
    if (!map().grid.segment(player.pos, position) || (position - player.pos).length() > 4)
        position = player.pos;
    GroundLocation location{region().definition.id, position};
    return inventoryDestinationAllowed(location) ? std::optional{location} : std::nullopt;
}
} // namespace d2x

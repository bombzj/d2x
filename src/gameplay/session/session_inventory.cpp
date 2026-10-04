#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "content/items/item_magic_loot.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
void GameSessionImpl::transmuteCube() {
    auto reject = [&] { simulation_->emit(InteractionFailed{{}, "No supported original cube recipe matches these items."}); };
    const auto carried = inventory_.contents(playerContainers_.backpack);
    const bool hasCube = std::any_of(carried.begin(), carried.end(), [&](EntityId id) {
        return inventory_.item(id)->definition == content_.cubeCode;
    });
    if (state().player.actions.dead || !hasCube || cursorItem()) { reject(); return; }
    const auto items = inventory_.contents(playerContainers_.cube);
    for (const auto &recipe : content_.socketRecipes) {
        if (recipe.version > 100 || state().population.difficulty < recipe.minimumDifficulty) continue;
        for (auto target : items) {
            const auto &original = *inventory_.item(target);
            const auto &base = *inventory_.catalog().find(original.definition);
            if (original.quantity != 1 || base.maxStack > 1 || !base.base.sockets.value_or(0) ||
                (recipe.itemType != "any" && !base.equipment.isType(recipe.itemType)) ||
                (recipe.quality && original.quality != *recipe.quality) ||
                (recipe.requiresSockets && !original.sockets) ||
                (recipe.requiresNoSockets && original.sockets)) continue;
            std::vector<ItemHandle> inputs{original.handle()};
            bool matched = true;
            for (const auto &material : recipe.materials) {
                unsigned count = 0;
                for (auto id : items) {
                    const auto &item = *inventory_.item(id);
                    if (item.quantity != 1 || std::any_of(inputs.begin(), inputs.end(),
                        [&](auto handle) { return handle.id == id; })) continue;
                    const auto *definition = inventory_.catalog().find(item.definition);
                    if ((material.type ? definition->equipment.isType(material.code) : item.definition == material.code) &&
                        (material.uniqueRow < 0 || (item.quality == ItemQuality::Unique && item.specialRow == material.uniqueRow))) {
                        inputs.push_back(item.handle());
                        if (++count == material.quantity) break;
                    }
                }
                matched &= count == material.quantity;
            }
            if (!matched || inputs.size() != items.size()) continue;
            if (original.revision == UINT64_MAX) { reject(); return; }
            InventoryService draft(ids_, inventory_.catalog(), {content_.stashLayout.columns, content_.stashLayout.rows},
                {content_.cubeLayout.columns, content_.cubeLayout.rows});
            draft.state_ = inventory_.state_; draft.itemProperties_ = inventory_.itemProperties_;
            draft.singleCarryUniques_ = inventory_.singleCarryUniques_;
            InventoryResult transaction;
            const auto preserved = original; // A reroll removes the original host and all its socket children.
            for (auto input : inputs) {
                if (!recipe.rerollMagic && input.id == target) continue;
                auto removed = draft.consume(input, 1, inventoryAccess());
                if (!removed) { reject(); return; }
                transaction.changes.insert(transaction.changes.end(), removed.changes.begin(), removed.changes.end());
            }
            if (recipe.rerollMagic) {
                const int level = std::clamp(recipe.level ? recipe.level :
                    recipe.playerLevelPercent * state().player.character.level / 100 +
                    recipe.itemLevelPercent * int(preserved.level) / 100, 1, 99);
                auto rolled = rollAffixItem(content_, base, ItemQuality::Magic, level,
                    draft.state_.creationRandom, characterDefinition_.code);
                if (!rolled.deferred.empty()) { reject(); return; }
                draft.state_.creationRandom = rolled.randomState;
                auto created = draft.createItem(preserved.definition, 1, std::get<ContainerLocation>(preserved.location),
                    unsigned(level), rolled.generation);
                if (!created) { reject(); return; }
                target = created.item;
                draft.state_.items.at(target).identified = true;
                transaction.changes.insert(transaction.changes.end(), created.changes.begin(), created.changes.end());
            }
            auto &host = draft.state_.items.at(target);
            const int limit = std::max(0, std::min({base.base.sockets.value_or(0),
                base.base.socketsByLevel[host.level <= 25 ? 0 : host.level <= 40 ? 1 : 2], base.width * base.height, 6}));
            if (!limit) { reject(); return; }
            const int rolled = recipe.minimum + int(limitedRandom(draft.state_.creationRandom,
                unsigned(recipe.maximum - recipe.minimum + 1)));
            host.sockets = unsigned(std::min(rolled, limit)); host.nativeFlags |= 0x800u;
            ++host.revision;
            transaction.item = target; transaction.transferred = 1;
            transaction.changes.push_back({target, host.revision, ItemChangeKind::PropertiesChanged,
                host.location, host.location, host.quantity});
            inventory_.state_ = std::move(draft.state_);
            publishInventory(std::move(transaction), {}); return;
        }
    }
    const auto &unsocket = content_.unsocketRecipe;
    if (unsocket.enabled && unsocket.version <= 100 &&
        state().population.difficulty >= unsocket.minimumDifficulty && items.size() == 3) {
        EntityId target;
        std::array<ItemHandle, 2> materials{};
        for (auto id : items) {
            const auto &item = *inventory_.item(id);
            if (item.quantity != 1) continue;
            for (size_t index = 0; index < materials.size(); ++index)
                if (item.definition == unsocket.materials[index]) materials[index] = item.handle();
            const auto *base = inventory_.catalog().find(item.definition);
            if (item.sockets && (unsocket.itemType == "any" || base->equipment.isType(unsocket.itemType))) target = id;
        }
        if (target && materials[0].id && materials[1].id && materials[0].id != materials[1].id &&
            target != materials[0].id && target != materials[1].id) {
            InventoryService draft(ids_, inventory_.catalog(), {content_.stashLayout.columns, content_.stashLayout.rows},
                {content_.cubeLayout.columns, content_.cubeLayout.rows});
            draft.state_ = inventory_.state_; draft.itemProperties_ = inventory_.itemProperties_;
            draft.singleCarryUniques_ = inventory_.singleCarryUniques_;
            auto &host = draft.state_.items.at(target);
            if (host.revision == UINT64_MAX) { reject(); return; }
            InventoryResult transaction;
            for (auto handle : materials) {
                auto removed = draft.consume(handle, 1, inventoryAccess());
                if (!removed) { reject(); return; }
                transaction.changes.insert(transaction.changes.end(), removed.changes.begin(), removed.changes.end());
            }
            host.socketedItems.clear(); host.socketRequiredLevel = 0;
            host.runewordRow = -1; host.runewordStats.clear(); host.nativeFlags &= ~0x4000000u;
            ++host.revision;
            transaction.item = target;
            transaction.changes.push_back({target, host.revision, ItemChangeKind::PropertiesChanged,
                host.location, host.location, host.quantity});
            inventory_.state_ = std::move(draft.state_);
            publishInventory(std::move(transaction), {});
            return;
        }
    }
    const bool khalim = items.size() == 4;
    const QuestId questId = khalim ? QuestId::KhalimsWill : QuestId::HoradricStaff;
    const std::span<const std::string> codes = khalim ? std::span<const std::string>(content_.khalimRecipe.inputs) :
        std::span<const std::string>(content_.staffRecipe.inputs);
    const auto &output = khalim ? content_.khalimRecipe.output : content_.staffRecipe.output;
    if (items.size() != codes.size() || quest(questId).stage >= questCompletionStage(questId)) { reject(); return; }
    std::vector<ItemHandle> inputs(codes.size());
    for (auto id : items) {
        const auto *item = inventory_.item(id);
        if (item->quantity != 1 || item->nativeQuestDifficulty < unsigned(state().population.difficulty)) { reject(); return; }
        for (size_t index = 0; index < inputs.size(); ++index)
            if (item->definition == codes[index]) inputs[index] = item->handle();
    }
    if (std::any_of(inputs.begin(), inputs.end(), [](const auto &input) { return !input.id; })) { reject(); return; }
    InventoryService draft(ids_, inventory_.catalog(), {content_.stashLayout.columns, content_.stashLayout.rows},
        {content_.cubeLayout.columns, content_.cubeLayout.rows});
    draft.state_ = inventory_.state_;
    draft.itemProperties_ = inventory_.itemProperties_;
    draft.singleCarryUniques_ = inventory_.singleCarryUniques_;
    InventoryResult transaction;
    for (auto input : inputs) {
        auto removed = draft.consume(input, 1, inventoryAccess());
        if (!removed) { reject(); return; }
        transaction.changes.insert(transaction.changes.end(), removed.changes.begin(), removed.changes.end());
    }
    const auto generation = questItemGeneration(output, draft.state_.creationRandom);
    auto created = draft.createItem(output, 1, AutoPlace{playerContainers_.cube}, unsigned(state().player.character.level), generation);
    if (!created) { reject(); return; }
    draft.state_.items.at(created.item).nativeQuestDifficulty = unsigned(state().population.difficulty);
    draft.state_.items.at(created.item).identified = true;
    transaction.item = created.item;
    transaction.changes.insert(transaction.changes.end(), created.changes.begin(), created.changes.end());
    inventory_.state_ = std::move(draft.state_);
    publishInventory(std::move(transaction), {});
    auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(questId));
    record.stage = khalim ? 3 : 5;
    simulation_->emit(QuestAdvanced{questId, record.stage});
}
void GameSessionImpl::createStarterEquipment() {
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
        if (body.empty()) {
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
bool GameSessionImpl::inventorySourceAllowed(EntityId id) const {
    const auto *item = inventory_.item(id);
    if (item)
        if (auto ground = std::get_if<GroundLocation>(&item->location))
            return inventoryDestinationAllowed(*ground);
    return true; // Missing IDs and stale revisions are reported by InventoryService.
}
InventoryError GameSessionImpl::previewInventory(const GameCommand &command) const {
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
            } else if constexpr (std::is_same_v<T, SocketItem>) {
                if (!inventorySourceAllowed(intent.filler.id)) return InventoryError::AccessDenied;
                const auto *target = inventory_.item(intent.host.id);
                const auto *position = target ? std::get_if<ContainerLocation>(&target->location) : nullptr;
                if (!position || position->container == playerContainers_.hirelingEquipment)
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
            } else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                return previewHirelingEquipment(intent);
            } else if constexpr (std::is_same_v<T, UseHirelingPotion>) {
                return previewHirelingPotion(intent.item);
            } else if constexpr (std::is_same_v<T, UseItem>) {
                const auto *source = inventory_.item(intent.item.id);
                if (source && source->definition == content_.prisonOfIce.scroll) {
                    if (auto error = inventory_.checkHandle(intent.item); error != InventoryError::None) return error;
                    const auto *location = std::get_if<ContainerLocation>(&source->location);
                    return location && location->container == playerContainers_.backpack && !state().player.actions.dead &&
                        source->nativeQuestDifficulty >= unsigned(state().population.difficulty) && quest(QuestId::PrisonOfIce).stage >= 5 &&
                        !(quest(QuestId::PrisonOfIce).flags & 2u) ? InventoryError::None : InventoryError::AccessDenied;
                }
                if (source && source->definition == content_.goldenBird.potion) {
                    if (auto error = inventory_.checkHandle(intent.item); error != InventoryError::None) return error;
                    const auto *location = std::get_if<ContainerLocation>(&source->location);
                    return location && location->container == playerContainers_.backpack &&
                        !state().player.actions.dead && state().player.resources.hp > 0 &&
                        source->nativeQuestDifficulty >= unsigned(state().population.difficulty) &&
                        quest(QuestId::GoldenBird).stage == 6 && (quest(QuestId::GoldenBird).flags & 1u)
                        ? InventoryError::None : InventoryError::AccessDenied;
                }
                if (source && source->definition == "ass") {
                    if (auto error = inventory_.checkHandle(intent.item); error != InventoryError::None) return error;
                    const auto *location = std::get_if<ContainerLocation>(&source->location);
                    return location && location->container == playerContainers_.backpack &&
                        !state().player.actions.dead && state().player.resources.hp > 0 &&
                        source->nativeQuestDifficulty >= unsigned(state().population.difficulty) &&
                        (quest(QuestId::RadamentsLair).flags & radamentBookPending)
                        ? InventoryError::None : InventoryError::AccessDenied;
                }
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
void GameSessionImpl::executeInventory(const GameCommand &command) {
    std::visit(
        [&](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SwapItems> ||
                          std::is_same_v<T, SplitStack> || std::is_same_v<T, MergeStacks> ||
                          std::is_same_v<T, LoadBook> || std::is_same_v<T, SocketItem> ||
                          std::is_same_v<T, EquipBelt> || std::is_same_v<T, TransferItem> ||
                          std::is_same_v<T, EquipItem>) {
                EntityId requested;
                if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, EquipBelt> ||
                              std::is_same_v<T, TransferItem> || std::is_same_v<T, EquipItem>)
                    requested = intent.item.id;
                else if constexpr (std::is_same_v<T, SwapItems>)
                    requested = intent.first.id;
                else if constexpr (std::is_same_v<T, SocketItem>)
                    requested = intent.filler.id;
                else if constexpr (std::is_same_v<T, LoadBook>)
                    requested = intent.scroll.id;
                else
                    requested = intent.source.id;
                auto error = previewInventory(command);
                if (error != InventoryError::None) {
                    simulation_->emit(InventoryRejected{requested, error});
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
                        simulation_->emit(BeltEquipped{});
                } else if constexpr (std::is_same_v<T, SocketItem>)
                    publishInventory(inventory_.socket(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, LoadBook>)
                    publishInventory(inventory_.loadBook(intent, inventoryAccess()), requested);
                else
                    publishInventory(inventory_.merge(intent, inventoryAccess()), requested);
            }
        },
        command);
}
std::optional<GroundLocation> GameSessionImpl::dropLocation() const {
    const auto &player = state().player;
    if (player.actions.dead)
        return std::nullopt;
    Vec position = map().grid.nearest(player.movement.pos + player.movement.look.unit());
    if (!map().grid.segment(player.movement.pos, position) || (position - player.movement.pos).length() > 4)
        position = player.movement.pos;
    GroundLocation location{region().definition.id, position};
    return inventoryDestinationAllowed(location) ? std::optional{location} : std::nullopt;
}
} // namespace d2x

#include "debug_inventory.hpp"
#include "gameplay/session/session.hpp"
#include "presentation/scene_view.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace d2x {
void debugItemInspect(const nlohmann::json &request, nlohmann::json &result,
                      const GameSession &session) {
    const auto &rawId = request.at("id");
    if (!rawId.is_number_unsigned() || rawId.get<uint64_t>() == 0)
        throw std::runtime_error("id must be a positive item entity ID");
    const auto *item = session.inventory().item(EntityId{rawId.get<uint64_t>()});
    if (!item)
        throw std::runtime_error("Unknown item");
    const auto *definition = session.inventory().catalog().find(item->definition);
    constexpr std::array<const char *, 7> qualities{"normal", "magic", "rare", "set", "unique",
                                                   "superior", "inferior"};
    result["item"] = {{"id", item->id.value}, {"revision", item->revision},
                      {"code", item->definition}, {"name", definition->name},
                      {"quantity", item->quantity}, {"level", item->level},
                      {"charges", item->charges},
                      {"durability", item->durability}, {"maxDurability", definition->maxDurability},
                      {"defense", item->defense}, {"quality", qualities.at(size_t(item->quality))},
                      {"identified", item->identified},
                      {"specialRow", item->specialRow}, {"gradeRow", item->gradeRow},
                      {"requiredLevel", item->requiredLevel},
                      {"rarePrefixRow", item->rarePrefixRow},
                      {"rareSuffixRow", item->rareSuffixRow},
                      {"propertyRolls", item->propertyRolls}};
    auto &details = result["item"];
    if (item->specialRow >= 0) {
        const auto &records = item->quality == ItemQuality::Unique ? session.content().uniqueItems
                                                                    : session.content().setItems;
        auto record = std::find_if(records.begin(), records.end(), [&](const auto &candidate) {
            return int32_t(candidate.row) == item->specialRow;
        });
        if (record != records.end())
            details["specialName"] = record->name;
    }
    details["affixes"] = nlohmann::json::array();
    for (const auto &affix : item->affixes)
        details["affixes"].push_back({{"prefix", affix.prefix}, {"row", affix.row},
                                       {"propertyRolls", affix.propertyRolls}});
    details["appearance"] = {{"component", definition->appearance.component},
                             {"token", definition->appearance.token},
                             {"body", definition->appearance.body}};
    if (const auto *ground = std::get_if<GroundLocation>(&item->location))
        details["location"] = {{"kind", "ground"}, {"region", int(ground->region)},
                               {"x", ground->position.x}, {"y", ground->position.y}};
    else {
        const auto &container = std::get<ContainerLocation>(item->location);
        details["location"] = {{"kind", "container"}, {"container", container.container.value},
                               {"x", container.cell.x}, {"y", container.cell.y}};
    }
}
void debugItemMove(const nlohmann::json &request, nlohmann::json &result,
                   GameSession &session, SceneView &view) {
    const auto &rawId = request.at("id");
    if (!rawId.is_number_unsigned() || rawId.get<uint64_t>() == 0)
        throw std::runtime_error("id must be a positive item entity ID");
    const auto *item = session.inventory().item(EntityId{rawId.get<uint64_t>()});
    if (!item)
        throw std::runtime_error("Unknown item");
    const auto *source = std::get_if<ContainerLocation>(&item->location);
    if (!source)
        throw std::runtime_error("Use pickup for ground items");

    const auto &containers = session.playerContainers();
    const auto to = request.at("to").get<std::string>();
    EntityId container;
    if (to == "backpack") container = containers.backpack;
    else if (to == "belt") container = containers.belt;
    else if (to == "stash") container = containers.stash;
    else if (to == "cube") container = containers.cube;
    else throw std::runtime_error("to must be backpack, belt, stash, or cube");
    const bool hasX = request.contains("x"), hasY = request.contains("y");
    if (hasX != hasY)
        throw std::runtime_error("x and y must be supplied together");
    ItemDestination destination = AutoPlace{container};
    if (hasX)
        destination = ContainerLocation{container, {request.at("x").get<int>(),
                                                    request.at("y").get<int>()}};
    if (!hasX && source->container == container)
        throw std::runtime_error("Specify x and y to move within the same container");
    const auto target = hasX ? session.inventory().itemAt(container,
        {request.at("x").get<int>(), request.at("y").get<int>()}) : EntityId{};

    GameCommand intent;
    if (source->container == containers.equipment)
        intent = EquipItem{item->handle(), std::nullopt, destination};
    else if (source->container == containers.beltEquipment)
        intent = EquipBelt{item->handle(), destination};
    else if (target && target != item->id)
        intent = MergeStacks{item->handle(), session.inventory().item(target)->handle()};
    else if (!hasX && to != "belt")
        intent = TransferItem{item->handle(), container};
    else
        intent = MoveItem{item->handle(), destination};
    if (auto error = session.previewInventory(intent); error != InventoryError::None)
        throw std::runtime_error(inventoryErrorText(error));
    const EntityId id = item->id;
    session.submit(std::move(intent));
    session.tick(0);
    view.advance(0);
    bool applied = false;
    for (const auto &event : session.events()) {
        if (auto rejected = std::get_if<InventoryRejected>(&event); rejected && rejected->item == id)
            throw std::runtime_error(inventoryErrorText(rejected->error));
        if (auto accepted = std::get_if<InventoryApplied>(&event); accepted && accepted->requested == id) {
            result["transferred"] = accepted->transferred;
            applied = true;
        }
    }
    if (!applied)
        throw std::runtime_error("Item move produced no inventory result");
    result["item"] = id.value;
    result["removedByMerge"] = session.inventory().item(id) == nullptr;
}
} // namespace d2x

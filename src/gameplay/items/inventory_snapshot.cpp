#include "inventory.hpp"
#include <array>
#include <limits>
#include <set>
#include <stdexcept>

namespace d2x {
void InventoryService::validateSnapshot(const InventoryState &state, const PlayerContainers &containers,
                                        EntityId player) const {
    auto require = [](bool condition, const char *reason) {
        if (!condition)
            throw std::runtime_error(std::string("Invalid save inventory: ") + reason);
    };
    const std::array ids{containers.backpack, containers.belt, containers.stash, containers.beltEquipment};
    const std::array kinds{ContainerKind::Backpack, ContainerKind::Belt, ContainerKind::Stash,
                           ContainerKind::BeltEquipment};
    const std::array widths{10, 4, 6, 2}, heights{4, 0, 4, 1};
    std::set<EntityId> unique(ids.begin(), ids.end());
    require(unique.size() == 4 && state.containers.size() == 4, "player container set");
    std::map<EntityId, std::vector<bool>> occupied;
    for (size_t i = 0; i < ids.size(); ++i) {
        auto found = state.containers.find(ids[i]);
        require(bool(ids[i]) && found != state.containers.end(), "missing container");
        const auto &c = found->second;
        require(c.id == ids[i] && c.spec.owner == player && c.spec.kind == kinds[i] &&
                    c.spec.columns == widths[i] &&
                    (heights[i] ? c.spec.rows == heights[i] : c.spec.rows >= 1 && c.spec.rows <= 4),
                "container ownership or dimensions");
        occupied.emplace(c.id, std::vector<bool>(size_t(c.spec.columns * c.spec.rows)));
    }
    int beltRows = 1, equipped = 0;
    for (const auto &[id, item] : state.items) {
        require(bool(id) && id == item.id && !state.containers.contains(id), "item ID");
        auto def = catalog_.find(item.definition);
        require(def != nullptr, "unknown definition");
        require(item.quantity > 0 && item.quantity <= def->maxStack &&
                    item.durability <= def->maxDurability && item.revision > 0 && item.level > 0 &&
                    item.level <= 99 && int(item.quality) >= 0 &&
                    int(item.quality) <= int(ItemQuality::Unique),
                "item parameters");
        auto location = std::get_if<ContainerLocation>(&item.location);
        if (!location)
            continue; // GameSession checks the region and world position.
        auto found = state.containers.find(location->container);
        require(found != state.containers.end(), "item container reference");
        const auto &c = found->second.spec;
        auto cell = location->cell;
        require(cell.x >= 0 && cell.y >= 0 && def->width <= c.columns && def->height <= c.rows &&
                    cell.x <= c.columns - def->width && cell.y <= c.rows - def->height,
                "item rectangle");
        if (c.kind == ContainerKind::Belt)
            require(def->beltAllowed && def->width == 1 && def->height == 1, "belt item eligibility");
        if (c.kind == ContainerKind::BeltEquipment) {
            require(++equipped == 1 && def->beltRows >= 1 && def->beltRows <= 4 && item.quantity == 1 &&
                        cell == Cell{},
                    "equipped belt");
            beltRows = def->beltRows;
        }
        auto &cells = occupied.at(location->container);
        for (int y = cell.y; y < cell.y + def->height; ++y)
            for (int x = cell.x; x < cell.x + def->width; ++x) {
                auto index = size_t(y * c.columns + x);
                require(!cells[index], "overlapping items");
                cells[index] = true;
            }
    }
    require(state.containers.at(containers.belt).spec.rows == beltRows,
            "belt capacity differs from equipment");
}
} // namespace d2x

#include "inventory.hpp"
#include <algorithm>
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
    std::vector<EntityId> ids{containers.backpack, containers.belt, containers.stash,
                              containers.beltEquipment, containers.equipment, containers.hirelingEquipment};
    std::vector<ContainerKind> kinds{ContainerKind::Backpack, ContainerKind::Belt, ContainerKind::Stash,
                                     ContainerKind::BeltEquipment, ContainerKind::Equipment, ContainerKind::Equipment};
    std::vector<int> widths{10, 4, stashDimensions_.x, 2, int(EquipmentSlot::Count), int(EquipmentSlot::Count)};
    std::vector<int> heights{4, 0, stashDimensions_.y, 1, 1, 1};
    if (cubeDimensions_.x > 0 && cubeDimensions_.y > 0) {
        ids.push_back(containers.cube);
        kinds.push_back(ContainerKind::Cube);
        widths.push_back(cubeDimensions_.x);
        heights.push_back(cubeDimensions_.y);
    } else
        require(!containers.cube, "cube container in classic profile");
    std::set<EntityId> unique(ids.begin(), ids.end());
    require(unique.size() == ids.size() && state.containers.size() == ids.size(), "player container set");
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
        const bool nativeNormalCharm = item.nativeProperties && item.quality == ItemQuality::Normal &&
                                       def->equipment.isType("char");
        if (def->family == ItemFamily::Armor)
            require(def->base.minDefense && def->base.maxDefense &&
                        item.defense >= (item.quality == ItemQuality::Inferior ?
                            std::max(1, *def->base.minDefense * 75 / 100) : *def->base.minDefense) &&
                        item.defense <= (item.quality == ItemQuality::Inferior ?
                            std::max(1, *def->base.maxDefense * 75 / 100) :
                            *def->base.maxDefense + (propertyValue(item, "item_armor_percent") ? 1 : 0)),
                    "rolled armor defense");
        else
            require(item.defense == 0, "defense on non-armor item");
        require((item.quantity > 0 || retainsEmptyStack(item)) &&
                    item.quantity <= maximumStack(item) &&
                    item.charges <= def->bookCapacity &&
                    item.durability <= maximumDurability(item) && item.revision > 0 && item.level > 0 &&
                    item.level <= 99 && int(item.quality) >= 0 &&
                    int(item.quality) <= int(ItemQuality::Inferior) &&
                    (item.identified || nativeNormalCharm || (item.quality == ItemQuality::Magic ||
                                         item.quality == ItemQuality::Rare ||
                                         item.quality == ItemQuality::Set ||
                                         item.quality == ItemQuality::Unique)) &&
                    item.specialRow >= -1 && item.gradeRow >= -1 &&
                    item.rarePrefixRow >= -1 && item.rareSuffixRow >= -1 &&
                    item.requiredLevel >= 0 && item.requiredLevel <= 99 &&
                    item.grantedSkill >= -1 &&
                    item.propertyRolls.size() <= 16 && item.affixes.size() <= 6 &&
                    (item.quality != ItemQuality::Normal ||
                     (item.specialRow == -1 && item.gradeRow == -1 &&
                      item.propertyRolls.empty() &&
                      (item.affixes.empty() || (nativeNormalCharm && item.affixes.size() == 1)))) &&
                    ((item.quality == ItemQuality::Set || item.quality == ItemQuality::Unique) ==
                     (item.specialRow >= 0)),
                "item parameters");
        auto location = std::get_if<ContainerLocation>(&item.location);
        require(!location || !def->equipment.isType("gold"), "gold must be on ground or in wallet");
        if (!location)
            continue; // GameSession checks the region and world position.
        auto found = state.containers.find(location->container);
        require(found != state.containers.end(), "item container reference");
        require(checkCarryLimit(item, item.location, state) == InventoryError::None,
            "duplicate carry1 unique item");
        const auto &c = found->second.spec;
        auto cell = location->cell;
        if (c.kind == ContainerKind::Equipment) {
            require(cell.y == 0 && cell.x >= 0 && cell.x < int(EquipmentSlot::Count) &&
                        EquipmentSlot(cell.x) != EquipmentSlot::Belt &&
                        def->equipment.fits(EquipmentSlot(cell.x)),
                    "equipment slot eligibility");
            auto &cells = occupied.at(location->container);
            require(!cells[size_t(cell.x)], "overlapping equipment");
            cells[size_t(cell.x)] = true;
            continue;
        }
        require(cell.x >= 0 && cell.y >= 0 && def->width <= c.columns && def->height <= c.rows &&
                    cell.x <= c.columns - def->width && cell.y <= c.rows - def->height,
                "item rectangle");
        if (c.kind == ContainerKind::Cube)
            require(!def->opensCube, "nested cube");
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

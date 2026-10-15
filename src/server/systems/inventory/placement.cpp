#include "planning.hpp"
#include "gameplay/items/inventory_order.hpp"
#include <limits>

namespace d2x::server::inventory::detail {
const ItemInstance *Draft::resolve(ItemHandle handle) const {
    const auto found = edit.inventory.items.find(handle.id);
    return found != edit.inventory.items.end() && found->second.revision == handle.revision ? &found->second : nullptr;
}
const ContainerState *Draft::container(EntityId id) const {
    const auto found = edit.inventory.containers.find(id);
    return found == edit.inventory.containers.end() ? nullptr : &found->second;
}
bool Draft::owned(ContainerLocation location) const {
    const auto *c = container(location.container);
    return c && c->spec.owner == player.actor;
}
EntityId Draft::at(ContainerLocation location) const {
    for (const auto &[id, item] : edit.inventory.items)
        if (item.location == ItemLocation{location}) return id;
    return {};
}
EntityId Draft::equipped(EquipmentSlot slot) const {
    return at(slot == EquipmentSlot::Belt ? ContainerLocation{containers().beltEquipment, {}}
        : ContainerLocation{containers().equipment, {int(slot), 0}});
}
bool Draft::fits(EntityId id, ContainerLocation target, EntityId ignore) const {
    const auto &item = edit.inventory.items.at(id);
    const auto *def = catalog.find(item.definition);
    const auto *c = container(target.container);
    if (!def || !c || !owned(target) || def->equipment.isType("gold")) return false;
    const bool slot = c->spec.kind == ContainerKind::Cursor || c->spec.kind == ContainerKind::Equipment ||
        c->spec.kind == ContainerKind::BeltEquipment;
    const int width = slot ? 1 : def->width, height = slot ? 1 : def->height;
    if (width < 1 || height < 1 || target.cell.x < 0 || target.cell.y < 0 ||
        target.cell.x > c->spec.columns - width || target.cell.y > c->spec.rows - height) return false;
    if (c->spec.kind == ContainerKind::Cursor && target.cell != Cell{}) return false;
    if (c->spec.kind == ContainerKind::Belt && (!def->beltAllowed || width != 1 || height != 1)) return false;
    for (const auto &[key, other] : edit.inventory.items) {
        if (key == id || key == ignore) continue;
        const auto *position = std::get_if<ContainerLocation>(&other.location);
        if (!position || position->container != target.container) continue;
        const auto *otherDef = catalog.find(other.definition);
        if (!otherDef) return false;
        const int w = slot ? 1 : otherDef->width, h = slot ? 1 : otherDef->height;
        if (target.cell.x < position->cell.x + w && target.cell.x + width > position->cell.x &&
            target.cell.y < position->cell.y + h && target.cell.y + height > position->cell.y) return false;
    }
    return true;
}
std::optional<ContainerLocation> Draft::space(EntityId id, EntityId destination) const {
    const auto *c = container(destination);
    if (!c) return {};
    if(c->spec.kind==ContainerKind::Belt) {
        const auto *base=catalog.find(edit.inventory.items.at(id).definition);
        if(!base || !base->beltAllowed) return {};
        // GetFreeBeltSlot prefers a matching potion in the bottom row. Different
        // grades of health, mana and rejuvenation potions share their column.
        for(int x=0;x<c->spec.columns;++x) {
            const auto bottom=at({destination,{x,0}});
            const auto *other=bottom?catalog.find(edit.inventory.items.at(bottom).definition):nullptr;
            if(!other) continue;
            bool matching=other->code==base->code;
            for(const auto type:{"hpot","mpot","rpot"}) matching=matching || (base->equipment.isType(type) && other->equipment.isType(type));
            if(matching) for(int y=0;y<c->spec.rows;++y) {
                const ContainerLocation target{destination,{x,y}};
                if(fits(id,target)) return target;
            }
        }
        if(base->autoBelt) for(int x=0;x<c->spec.columns;++x) {
            const ContainerLocation target{destination,{x,0}};
            if(fits(id,target)) return target;
        }
        return {};
    }
    for (int y = 0; y < c->spec.rows; ++y)
        for (int x = 0; x < c->spec.columns; ++x) {
            const ContainerLocation target{destination, {x, y}};
            if (fits(id, target)) return target;
        }
    return {};
}
DomainStatus Draft::move(EntityId id, ContainerLocation destination) {
    auto &item = edit.inventory.items.at(id);
    if (item.location == ItemLocation{destination}) return DomainStatus::InvalidRequest;
    if (item.revision == std::numeric_limits<uint64_t>::max()) return DomainStatus::Capacity;
    edit.changes.push_back({id, item.revision + 1, ItemChangeKind::Moved, item.location, destination, item.quantity});
    item.location = destination;
    ++item.revision;
    reinsertInventoryItem(edit.inventory,id);
    return DomainStatus::Applied;
}
DomainStatus Draft::resizeBelt(int rows) {
    if (rows < 1 || rows > 4) return DomainStatus::Unavailable;
    auto &belt = edit.inventory.containers.at(containers().belt);
    belt.spec.rows = rows;
    for (const auto &[id, item] : edit.inventory.items) {
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location || location->container != belt.id || location->cell.y < rows) continue;
        const auto target = space(id, containers().backpack);
        if (target) {
            const auto result = move(id, *target);
            if (result != DomainStatus::Applied) return result;
        } else {
            if (item.revision == UINT64_MAX) return DomainStatus::Capacity;
            auto copy = item; ++copy.revision;
            edit.spilled.push_back(std::move(copy));
            edit.changes.push_back({id, item.revision + 1, ItemChangeKind::Removed, item.location, {}, 0});
        }
    }
    for (const auto &item : edit.spilled) edit.inventory.items.erase(item.id);
    return DomainStatus::Applied;
}
}

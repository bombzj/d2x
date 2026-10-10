#include "planning.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
namespace d2x::server::inventory::detail {
namespace {
int64_t property(const EquipmentRules &rules, EntityId id, int level, std::string_view name) {
    int64_t result = 0;
    for (const auto &stat : rules.at(id, level).stats)
        if (!stat.layer && (stat.effect.empty() ? stat.name : stat.effect) == name) result += stat.value;
    return result;
}
bool equal(const ItemInstance &a, const ItemInstance &b, const EquipmentRules &rules, int level) {
    if (a.definition != b.definition || a.quality != b.quality || a.gradeRow != b.gradeRow || a.specialRow != b.specialRow ||
        (a.quality != ItemQuality::Normal && a.quality != ItemQuality::Superior && a.quality != ItemQuality::Inferior) ||
        ((a.nativeFlags ^ b.nativeFlags) & 0x00400000u) || a.grantedSkill != b.grantedSkill || a.sockets || b.sockets) return false;
    for (const auto stat : {"mindamage", "maxdamage", "secondary_mindamage", "secondary_maxdamage",
        "item_throw_mindamage", "item_throw_maxdamage", "item_mindamage_percent", "item_maxdamage_percent"})
        if (property(rules, a.id, level, stat) != property(rules, b.id, level, stat)) return false;
    return !property(rules, a.id, level, "item_numsockets") && !property(rules, b.id, level, "item_numsockets");
}
}
unsigned Draft::stackSpace(const ItemInstance &a,const ItemInstance &b) const {
    const auto *from=std::get_if<ContainerLocation>(&a.location),*to=std::get_if<ContainerLocation>(&b.location);
    const auto permitted=[&](EntityId id){return (storage && id==containers().stash) || id==containers().backpack || id==containers().cursor || id==containers().equipment;};
    const auto *base=catalog.find(b.definition);
    if(a.id==b.id || !from || !to || !owned(*from) || !owned(*to) || !permitted(from->container) || !permitted(to->container) ||
        !base || base->maxStack<=1 || base->bookCapacity || !equal(a,b,rules,player.persistent.player.level)) return 0;
    const int64_t maximum=int64_t(base->maxStack)+property(rules,b.id,player.persistent.player.level,"item_extra_stack");
    return maximum>1 && maximum<=511 && b.quantity<unsigned(maximum)?unsigned(maximum)-b.quantity:0;
}
DomainStatus Draft::merge(const MergeStacks &command) {
    const auto *a = resolve(command.source), *b = resolve(command.target);
    if (!a || !b) return DomainStatus::Stale;
    if (a->id == b->id) return DomainStatus::InvalidRequest;
    const auto *from = std::get_if<ContainerLocation>(&a->location), *to = std::get_if<ContainerLocation>(&b->location);
    if (!from || !to || !owned(*from) || !owned(*to)) return DomainStatus::InvalidRequest;
    const auto permitted = [&](EntityId id) {
        return (storage && id == containers().stash) || id == containers().backpack || id == containers().cursor || id == containers().equipment;
    };
    if (!permitted(from->container) || !permitted(to->container)) return DomainStatus::InvalidRequest;
    const auto *definition = catalog.find(b->definition);
    if (!definition || definition->maxStack <= 1 || definition->bookCapacity ||
        !equal(*a, *b, rules, player.persistent.player.level)) return DomainStatus::InvalidRequest;
    const int64_t maximum = int64_t(definition->maxStack) + property(rules, b->id, player.persistent.player.level, "item_extra_stack");
    if (maximum <= 1 || maximum > 511) return DomainStatus::Unavailable;
    if (!a->quantity || b->quantity > maximum) return DomainStatus::InvalidRequest;
    const unsigned available = stackSpace(*a,*b);
    const unsigned amount = command.quantity ? command.quantity : std::min(a->quantity, available);
    if (!amount || amount > a->quantity || amount > available) return DomainStatus::Conflict;
    if (a->revision == UINT64_MAX || b->revision == UINT64_MAX) return DomainStatus::Capacity;
    auto &source = edit.inventory.items.at(a->id), &target = edit.inventory.items.at(b->id);
    const unsigned left = source.quantity - amount;
    if (!left && rules.at(source.id, player.persistent.player.level).maximumDurability)
        target.durability = std::min(target.durability, source.durability);
    target.quantity += amount; ++target.revision;
    edit.changes.push_back({target.id, target.revision, ItemChangeKind::QuantityChanged, target.location, target.location, target.quantity});
    ++source.revision; source.quantity = left;
    edit.changes.push_back({source.id, source.revision, left ? ItemChangeKind::QuantityChanged : ItemChangeKind::Removed,
        source.location, left ? std::optional<ItemLocation>{source.location} : std::nullopt, left});
    if (!left) edit.inventory.items.erase(source.id);
    return DomainStatus::Applied;
}
DomainStatus Draft::loadBook(const LoadBook &command) {
    const auto *a = resolve(command.scroll), *b = resolve(command.book);
    if (!a || !b) return DomainStatus::Stale;
    if (a->id == b->id) return DomainStatus::InvalidRequest;
    const auto *from = std::get_if<ContainerLocation>(&a->location), *to = std::get_if<ContainerLocation>(&b->location);
    const auto permitted = [&](EntityId id) { return (storage && id == containers().stash) || (cube && id == containers().cube) || id == containers().backpack || id == containers().cursor; };
    if (!from || !to || !owned(*from) || !owned(*to) || !permitted(from->container) || !permitted(to->container))
        return DomainStatus::InvalidRequest;
    const auto *definition = catalog.find(b->definition);
    if (!definition || !definition->bookCapacity || a->quality != ItemQuality::Normal || b->quality != ItemQuality::Normal ||
        a->quantity != 1 || b->quantity != 1 || (a->definition != definition->bookScroll && a->definition != b->definition))
        return DomainStatus::InvalidRequest;
    const bool book = a->definition == b->definition;
    if (b->charges > definition->bookCapacity || (book && a->charges > definition->bookCapacity)) return DomainStatus::InvalidRequest;
    const unsigned amount = std::min(book ? a->charges : 1, definition->bookCapacity - b->charges);
    if (!amount) return DomainStatus::Conflict;
    if (a->revision == UINT64_MAX || b->revision == UINT64_MAX) return DomainStatus::Capacity;
    auto &source = edit.inventory.items.at(a->id), &target = edit.inventory.items.at(b->id);
    target.charges += amount; ++target.revision;
    edit.changes.push_back({target.id, target.revision, ItemChangeKind::QuantityChanged, target.location, target.location, target.quantity});
    const unsigned left = book ? source.charges - amount : 0;
    ++source.revision; source.charges = left;
    edit.changes.push_back({source.id, source.revision, left ? ItemChangeKind::QuantityChanged : ItemChangeKind::Removed,
        source.location, left ? std::optional<ItemLocation>{source.location} : std::nullopt, left ? 1u : 0u});
    if (!left) edit.inventory.items.erase(source.id);
    return DomainStatus::Applied;
}
DomainStatus Draft::mergeCarried(EntityId id) {
    // Ground pickup uses a private cursor bridge; manual authorization remains
    // unchanged. Corpse recovery has its own original equip/belt/pack ordering.
    const auto *definition = catalog.find(edit.inventory.items.at(id).definition);
    if (!definition) return DomainStatus::Unavailable;
    if (!definition->autoStack && !definition->bookCapacity && !definition->equipment.isType("scro")) return DomainStatus::Applied;
    std::vector<EntityId> targets;
    for (const auto &[other, item] : edit.inventory.items) {
        const auto *at = std::get_if<ContainerLocation>(&item.location);
        if (other != id && at && (at->container == containers().backpack || at->container == containers().equipment))
            targets.push_back(other);
    }
    for (const auto target : targets) {
        const auto current = edit.inventory.items.find(id);
        if (current == edit.inventory.items.end()) break;
        const auto &other = edit.inventory.items.at(target);
        const auto *book = catalog.find(other.definition);
        DomainStatus result = DomainStatus::InvalidRequest;
        if (book && book->bookCapacity && (current->second.definition == book->bookScroll || current->second.definition == other.definition))
            result = loadBook({current->second.handle(), other.handle()});
        else if (stackSpace(current->second, other))
            result = merge({current->second.handle(), other.handle(), 0});
        if (result == DomainStatus::Capacity || result == DomainStatus::Unavailable) return result;
    }
    return DomainStatus::Applied;
}
}

#include "d2s_inventory.hpp"
#include "gameplay/items/quality.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/socket_data.hpp"
#include "content/items/cube_data.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
void require(bool condition, const std::string &reason) {
    if (!condition) throw std::runtime_error("D2S item mapping: " + reason);
}
constexpr auto &qualities = nativeItemQualities;
std::vector<D2sStat> extraProperties(const ClassicData &content, const ItemInstance &item) {
    std::vector<D2sStat> result;
    auto check = [&](const auto &properties) {
        for (const auto &property : properties) {
            auto found = std::find_if(content.properties.begin(), content.properties.end(),
                [&](const auto &entry) { return entry.code == property.code; });
            require(found != content.properties.end() && !found->operations.empty(), "unknown property " + property.code);
            for (const auto &operation : found->operations)
                switch (operation.function) {
                case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10:
                case 11: case 13: case 14: case 15: case 16: case 17: case 19: case 20: case 21: case 22: case 23: case 24: break;
                default: throw std::runtime_error("Cannot save " + item.definition + ": unsupported property " + property.code);
                }
        }
    };
    if (item.specialRow >= 0) {
        const auto &records = item.quality == ItemQuality::Unique ? content.uniqueItems : content.setItems;
        auto found = std::find_if(records.begin(), records.end(), [&](const auto &entry) { return int(entry.row) == item.specialRow; });
        require(found != records.end(), "special row");
        check(found->properties);
    }
    if (item.quality == ItemQuality::Superior)
        for (const auto &entry : content.superiorGrades)
            if (int(entry.row) == item.gradeRow) check(entry.properties);
    for (const auto &affix : item.affixes) {
        const auto &records = affix.prefix ? content.magicPrefixes : content.magicSuffixes;
        auto found = std::find_if(records.begin(), records.end(), [&](const auto &entry) { return int(entry.row) == affix.row; });
        require(found != records.end(), "affix row");
        check(found->properties);
    }
    return result;
}
} // namespace
void initializeD2sInventory(CharacterSaveData &snapshot, const ClassicData &content) {
    snapshot.player.id = EntityId{1};
    snapshot.nextEntityId = 2;
    auto add = [&](EntityId &id, ContainerKind kind, int width, int height) {
        id = EntityId{snapshot.nextEntityId++};
        snapshot.inventory.containers.emplace(id, ContainerState{id, {snapshot.player.id, kind, width, height}});
    };
    add(snapshot.containers.cursor, ContainerKind::Cursor, 1, 1);
    add(snapshot.containers.backpack, ContainerKind::Backpack, 10, 4);
    add(snapshot.containers.belt, ContainerKind::Belt, 4, 1);
    add(snapshot.containers.stash, ContainerKind::Stash, content.stashLayout.columns, content.stashLayout.rows);
    add(snapshot.containers.beltEquipment, ContainerKind::BeltEquipment, 2, 1);
    add(snapshot.containers.equipment, ContainerKind::Equipment, int(EquipmentSlot::Count), 1);
    add(snapshot.containers.hirelingEquipment, ContainerKind::Equipment, int(EquipmentSlot::Count), 1);
    add(snapshot.containers.cube, ContainerKind::Cube, content.cubeLayout.columns, content.cubeLayout.rows);
}
void importD2sItem(CharacterSaveData &snapshot, const D2sItem &source, const ClassicData &content,
                   bool hireling, EntityId corpse, EntityId socketHost, unsigned socketIndex) {
    const auto *definition = content.items.find(source.code);
    require(definition != nullptr && source.quality > 0 && source.quality < qualities.size(), "item identity");
    ItemInstance item;
    item.id = EntityId{snapshot.nextEntityId++};
    item.definition = source.code;
    item.level = source.level;
    item.identified = (source.flags & 0x10) != 0;
    item.quality = qualities[source.quality];
    item.defense = int(source.defense);
    item.durability = source.durability;
    item.quantity = definition->bookCapacity ? 1 : source.quantity;
    item.charges = definition->bookCapacity ? source.quantity : 0;
    item.nativeProperties = true;
    item.nativeSeed = source.seed;
    item.sockets = source.sockets;
    item.personalizedName = source.personalizedName;
    item.nativeFlags = source.flags;
    item.nativeFormat = source.format;
    item.nativeGraphic = source.graphic;
    item.nativeHasGraphic = source.hasGraphic;
    item.nativeAutoAffix = source.autoAffix;
    item.nativeMaxDurability = source.maxDurability;
    item.nativeQuestDifficulty = source.questDifficulty;
    for (const auto &stat : source.runewordStats) {
        require(stat.value >= INT32_MIN && stat.value <= INT32_MAX, "runeword stat range");
        item.runewordStats.push_back({int(stat.id), stat.parameter, int(stat.value)});
    }
    for (const auto &stat : source.stats) {
        require(stat.value >= INT32_MIN && stat.value <= INT32_MAX, "stat value range");
        item.savedStats.push_back({int(stat.id), stat.parameter, int(stat.value)});
    }
    for (size_t index = 0; index < source.setStats.size(); ++index)
        for (const auto &stat : source.setStats[index]) {
            require(stat.value >= INT32_MIN && stat.value <= INT32_MAX, "set stat value range");
            item.savedSetStats[index].push_back({int(stat.id), stat.parameter, int(stat.value)});
        }
    if (item.quality == ItemQuality::Unique || item.quality == ItemQuality::Set) {
        item.specialRow = int(source.fileIndex);
        const auto &records = item.quality == ItemQuality::Unique ? content.uniqueItems : content.setItems;
        auto found = std::find_if(records.begin(), records.end(), [&](const auto &entry) { return entry.row == source.fileIndex; });
        require(found != records.end() && (found->code == item.definition ||
            content.cubeBases.at(item.definition).normal == found->code ||
            content.cubeBases.at(item.definition).exceptional == found->code), "special item row/base mismatch");
        item.requiredLevel = found->requiredLevel;
    } else if (item.quality == ItemQuality::Superior || item.quality == ItemQuality::Inferior) {
        item.gradeRow = int(source.fileIndex);
        const auto &records = item.quality == ItemQuality::Superior ? content.superiorGrades : content.inferiorGrades;
        require(std::any_of(records.begin(), records.end(), [&](const auto &entry) {
            return entry.row == source.fileIndex;
        }), "unknown quality grade");
    }
    for (bool prefix : {true, false})
        for (auto index : prefix ? source.prefixes : source.suffixes) {
            if (!index) continue;
            const auto &records = prefix ? content.magicPrefixes : content.magicSuffixes;
            auto found = std::find_if(records.begin(), records.end(), [&](const auto &entry) { return entry.row == index - 1; });
            require(found != records.end(), "unknown affix ID");
            item.affixes.push_back({prefix, int32_t(index - 1), {}});
            item.requiredLevel = std::max(item.requiredLevel, found->requiredLevel);
        }
    if ((item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted)) {
        const auto suffixCount = content.tables.at("raresuffix").rows().size();
        require(source.rarePrefix > suffixCount && source.rareSuffix > 0 && source.rareSuffix <= suffixCount,
                "rare name IDs");
        item.rarePrefixRow = int(source.rarePrefix - suffixCount - 1);
        item.rareSuffixRow = int(source.rareSuffix - 1);
        require(std::any_of(content.rarePrefixes.begin(), content.rarePrefixes.end(), [&](const auto &entry) {
            return int(entry.row) == item.rarePrefixRow;
        }) && std::any_of(content.rareSuffixes.begin(), content.rareSuffixes.end(), [&](const auto &entry) {
            return int(entry.row) == item.rareSuffixRow;
        }), "unknown rare name rows");
    }
    updateCubeRequiredLevel(content, item);
    auto known=item;known.identified=true;
    if (socketHost) {
        require(source.durability <= itemMaximumDurability(content,known,resolveOwnItemStats(content,known,snapshot.player.level)), "socket durability exceeds total maximum");
        require(source.mode == 6 && definition->equipment.isType("sock") && source.socketedItems.empty() &&
            source.x == socketIndex && source.y == 0 && item.identified && item.quantity == 1, "socket child identity/order");
        item.location = SocketLocation{socketHost, socketIndex};
        snapshot.inventory.items.at(socketHost).socketedItems.push_back(std::move(item));
        return;
    }
    require(source.mode != 6, "orphan socket child");
    ContainerLocation location;
    if (corpse) {
        require(!hireling, "corpse hireling item");
        if (source.mode == 1) {
            require(source.body >= 1 && source.body <= 12 &&
                (source.body == 8 ? definition->beltRows > 0 :
                    definition->equipment.fits(EquipmentSlot(source.body - 1))), "corpse body slot");
            location = {corpse, {int(source.body - 1), 0}};
        } else {
            require(source.mode == 0 && source.page == 1, "unsupported corpse item location");
            location = {corpse, {int(EquipmentSlot::Count), 0}};
        }
        for (const auto &[id, previous] : snapshot.inventory.items)
            require(previous.location != ItemLocation{location}, "duplicate corpse item slot");
    } else if (source.mode == 1) {
        require(source.body >= 1 && source.body <= 12, "body slot");
        location = {hireling ? snapshot.containers.hirelingEquipment : snapshot.containers.equipment,
                    {int(source.body - 1), 0}};
        if (!hireling && source.body == 8) {
            location = {snapshot.containers.beltEquipment, {}};
            snapshot.inventory.containers.at(snapshot.containers.belt).spec.rows = definition->beltRows;
        }
    } else if (source.mode == 4) {
        require(!hireling, "cursor hireling item");
        location = {snapshot.containers.cursor, {}};
    } else if (source.mode == 2) {
        require(!hireling && source.x < 16 && source.y == 0, "belt coordinates");
        location = {snapshot.containers.belt, {int(source.x % 4), int(source.x / 4)}};
    } else {
        require(!hireling, "unequipped hireling item");
        EntityId container = source.page == 1 ? snapshot.containers.backpack
            : source.page == 4 ? snapshot.containers.cube : source.page == 5 ? snapshot.containers.stash : EntityId{};
        require(bool(container), "inventory page");
        location = {container, {int(source.x), int(source.y)}};
    }
    item.location = location;
    const auto hostId = item.id;
    snapshot.inventory.items.emplace(hostId, std::move(item));
    for (size_t index = 0; index < source.socketedItems.size(); ++index)
        importD2sItem(snapshot, source.socketedItems[index], content, false, {}, hostId, unsigned(index));
    auto &host = snapshot.inventory.items.at(hostId);
    host.socketRequiredLevel = socketRequiredLevel(content, host);
    const auto *word = matchRuneword(content, host);
    require(bool(source.flags & 0x4000000u) == bool(word), "runeword flag/sequence mismatch");
    if (word) {
        require(source.runewordId == unsigned(word->stringId), "runeword original TBL identity mismatch");
        host.runewordRow = word->row;
    }
    known=host;known.identified=true;
    require(source.durability <= itemMaximumDurability(content,known,resolveItemStats(content,known,snapshot.player.level)), "durability exceeds total maximum");
}
D2sItem exportD2sItem(const CharacterSaveData &snapshot, const ItemInstance &item, const ClassicData &content) {
    const auto *definition = content.items.find(item.definition);
    require(definition != nullptr, "unknown base item");
    D2sItem output;
    output.code = item.definition;
    output.level = item.level;
    output.seed = item.nativeSeed;
    output.flags = item.nativeProperties ? item.nativeFlags : (0x00800000u | (item.nativeFlags & 0x4400000u));
    output.flags = (output.flags & ~0x00080110u) | 0x00800000u | (item.identified ? 0x10u : 0u);
    if (definition->maxDurability && !item.durability) output.flags |= 0x100;
    output.format = item.nativeFormat;
    output.hasGraphic = item.nativeHasGraphic;
    output.graphic = item.nativeGraphic;
    output.autoAffix = item.nativeAutoAffix;
    output.questDifficulty = item.nativeQuestDifficulty;
    output.sockets = item.sockets;
    if (item.runewordRow >= 0) {
        const auto *word = matchRuneword(content, item);
        require(word && word->row == item.runewordRow, "runeword sequence/row");
        output.runewordId = unsigned(word->stringId);
        output.flags |= 0x4000000u;
        for (const auto &stat : item.runewordStats)
            output.runewordStats.push_back({uint16_t(stat.id), stat.value, stat.parameter});
    }
    for (const auto &child : item.socketedItems)
        output.socketedItems.push_back(exportD2sItem(snapshot, child, content));
    output.personalizedName = item.personalizedName;
    if (!item.personalizedName.empty()) output.flags |= 0x1000000u;
    if (item.sockets) output.flags |= 0x800u;
    if (content.tables.at(definition->base.sourceTable).number(definition->base.sourceRow, "compactsave").value_or(0))
        output.flags |= 0x00200000;
    output.quality = unsigned(std::find(qualities.begin() + 1, qualities.end(), item.quality) - qualities.begin());
    output.fileIndex = unsigned(std::max(0, item.specialRow >= 0 ? item.specialRow : item.gradeRow));
    size_t prefix = 0, suffix = 0;
    for (const auto &affix : item.affixes) {
        require(affix.row >= 0 && (affix.prefix ? prefix : suffix) < 3, "affix index");
        (affix.prefix ? output.prefixes[prefix++] : output.suffixes[suffix++]) = unsigned(affix.row + 1);
    }
    if ((item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted)) {
        output.rarePrefix = unsigned(item.rarePrefixRow + 1 + content.tables.at("raresuffix").rows().size());
        output.rareSuffix = unsigned(item.rareSuffixRow + 1);
    }
    output.book = item.definition == "ibk" || item.definition == "isc" ? 1 : 0;
    output.defense = unsigned(item.defense);
    output.durability = item.durability;
    output.quantity = definition->bookCapacity ? item.charges : item.quantity;
    if (item.nativeProperties) {
        for (const auto &stat : item.savedStats) output.stats.push_back({uint16_t(stat.id), stat.value, stat.parameter});
        for (size_t index = 0; index < item.savedSetStats.size(); ++index)
            for (const auto &stat : item.savedSetStats[index])
                output.setStats[index].push_back({uint16_t(stat.id), stat.value, stat.parameter});
        output.maxDurability = item.nativeMaxDurability;
    } else {
        const auto extras = extraProperties(content, item);
        auto identified = item;
        identified.identified = true;
        const auto resolved = resolveOwnItemStats(content, identified, snapshot.player.level);
        std::map<std::pair<int, int>, int64_t> stats;
        for (const auto &stat : resolved) {
            if (stat.name == "item_numsockets") continue; // Encoded in the native socket field.
            auto found = std::find_if(content.itemStats.begin(), content.itemStats.end(),
                [&](const auto &entry) { return entry.name == stat.name; });
            require(found != content.itemStats.end() && found->id, "stat identity");
            stats[{*found->id, stat.layer}] += stat.rawValue;
        }
        if (item.grantedSkill >= 0) stats[{107, item.grantedSkill}] += 1;
        for (const auto &stat : extras) stats[{stat.id, stat.parameter}] += stat.value;
        for (const auto &[key, value] : stats) output.stats.push_back({uint16_t(key.first), value, key.second});
        int base = int(definition->maxDurability);
        if (item.quality == ItemQuality::Inferior && base) base = std::max(1, base / 3);
        const bool indestructible = std::any_of(resolved.begin(), resolved.end(), [](const auto &stat) {
            return stat.name == "item_indesctructible" && stat.value;
        });
        output.maxDurability = unsigned(base && !indestructible && (item.nativeFlags&0x400000u)?base/2+1:base);
        if (item.quality == ItemQuality::Set)
            for (const auto &record : content.setItems) {
                if (int(record.row) != item.specialRow) continue;
                for (const auto &bonus : record.setBonuses) {
                    if (!bonus.perItem || record.setAddFunction != 2) continue;
                    require(bonus.pieces >= 2 && bonus.pieces <= 6 &&
                        (!bonus.property.directRoll || bonus.property.minimum == bonus.property.maximum),
                        "random set bonus has no saved roll");
                    const auto resolved = resolvePropertyStats(content, bonus.property,
                        bonus.property.minimum.value_or(0), snapshot.player.level);
                    require(!resolved.empty(), "unsupported set bonus " + bonus.property.code);
                    auto &list = output.setStats[size_t(bonus.pieces - 2)];
                    for (const auto &stat : resolved) {
                        auto definition = std::find_if(content.itemStats.begin(), content.itemStats.end(),
                            [&](const auto &entry) { return entry.name == stat.name; });
                        require(definition != content.itemStats.end() && definition->id, "set stat identity");
                        auto existing = std::find_if(list.begin(), list.end(), [&](const auto &entry) {
                            return entry.id == *definition->id && entry.parameter == stat.layer;
                        });
                        if (existing == list.end()) list.push_back({uint16_t(*definition->id), stat.rawValue, stat.layer});
                        else existing->value += stat.rawValue;
                    }
                }
            }
    }
    if (const auto *socket = std::get_if<SocketLocation>(&item.location)) {
        const auto found = snapshot.inventory.items.find(socket->host);
        require(found != snapshot.inventory.items.end() && socket->index < found->second.socketedItems.size() &&
            found->second.socketedItems[socket->index].id == item.id, "socket parent identity/order");
        output.mode = 6; output.page = 0; output.body = 0; output.x = socket->index; output.y = 0;
        return output;
    }
    const auto *location = std::get_if<ContainerLocation>(&item.location);
    require(location != nullptr, "ground item in character inventory");
    const auto &containers = snapshot.containers;
    const auto storage = snapshot.inventory.containers.find(location->container);
    require(storage != snapshot.inventory.containers.end(), "missing item container");
    if (storage->second.spec.kind == ContainerKind::Corpse) {
        require(location->cell.y == 0 && location->cell.x >= 0 &&
                location->cell.x <= int(EquipmentSlot::Count), "corpse item slot");
        if (location->cell.x < int(EquipmentSlot::Count)) {
            output.mode = 1; output.body = unsigned(location->cell.x + 1); output.page = 0;
        } else {
            output.mode = 0; output.page = 1; output.x = output.y = 0;
        }
    } else if (location->container == containers.equipment || location->container == containers.hirelingEquipment ||
        location->container == containers.beltEquipment) {
        output.mode = 1;
        output.body = location->container == containers.beltEquipment ? 8 : unsigned(location->cell.x + 1);
        output.page = 0;
    } else if (location->container == containers.cursor) {
        output.mode = 4; // D2Items.h: IMODE_ONCURSOR; native v96 location, no grid coordinates.
        output.page = 0;
    } else if (location->container == containers.belt) {
        output.mode = 2; output.page = 0;
        output.x = unsigned(location->cell.x + location->cell.y * 4);
    } else {
        output.page = location->container == containers.backpack ? 1
            : location->container == containers.cube ? 4 : location->container == containers.stash ? 5 : 0;
        require(output.page != 0, "unknown container");
        output.x = unsigned(location->cell.x); output.y = unsigned(location->cell.y);
    }
    return output;
}
} // namespace d2x

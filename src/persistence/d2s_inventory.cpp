#include "d2s_inventory.hpp"
#include "content/item_properties.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
void require(bool condition, const std::string &reason) {
    if (!condition) throw std::runtime_error("D2S item mapping: " + reason);
}
constexpr std::array<ItemQuality, 8> qualities{ItemQuality::Normal, ItemQuality::Inferior,
    ItemQuality::Normal, ItemQuality::Superior, ItemQuality::Magic, ItemQuality::Set,
    ItemQuality::Rare, ItemQuality::Unique};
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
                case 15: case 16: case 17: case 20: case 21: case 22: break;
                case 11: {
                    int skillId = -1;
                    const auto &skills = content.tables.at("skills");
                    for (size_t row = 0; row < skills.rows().size(); ++row)
                        if (skills.value(row, "skill") == property.parameter ||
                            std::to_string(skills.number(row, "Id").value_or(-1)) == property.parameter)
                            skillId = skills.number(row, "Id").value_or(-1);
                    const auto *skill = content.skills.find(skillId);
                    require(skill != nullptr, "trigger skill identity");
                    int level = property.maximum.value_or(0);
                    if (!level) level = std::clamp((int(item.level) - skill->requiredLevel) / 4 + 1, 1, std::max(1, skill->maximumRank));
                    else if (level < 0) level = std::max(1, (int(item.level) - skill->requiredLevel) /
                        std::max(1, -(std::max(1, 99 - skill->requiredLevel) / level)));
                    auto stat = std::find_if(content.itemStats.begin(), content.itemStats.end(),
                        [&](const auto &entry) { return entry.name == operation.stat; });
                    require(stat != content.itemStats.end() && stat->id, "trigger stat identity");
                    int chance = property.minimum.value_or(0);
                    result.push_back({uint16_t(*stat->id), chance > 0 ? chance : 5, (skillId << 6) | (level & 63)});
                    break;
                }
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
void initializeD2sInventory(SessionSnapshot &snapshot, const ClassicData &content) {
    snapshot.world.player.id = EntityId{1};
    snapshot.nextEntityId = 2;
    auto add = [&](EntityId &id, ContainerKind kind, int width, int height) {
        id = EntityId{snapshot.nextEntityId++};
        snapshot.inventory.containers.emplace(id, ContainerState{id, {snapshot.world.player.id, kind, width, height}});
    };
    add(snapshot.containers.backpack, ContainerKind::Backpack, 10, 4);
    add(snapshot.containers.belt, ContainerKind::Belt, 4, 1);
    add(snapshot.containers.stash, ContainerKind::Stash, content.stashLayout.columns, content.stashLayout.rows);
    add(snapshot.containers.beltEquipment, ContainerKind::BeltEquipment, 2, 1);
    add(snapshot.containers.equipment, ContainerKind::Equipment, int(EquipmentSlot::Count), 1);
    add(snapshot.containers.hirelingEquipment, ContainerKind::Equipment, int(EquipmentSlot::Count), 1);
    add(snapshot.containers.cube, ContainerKind::Cube, content.cubeLayout.columns, content.cubeLayout.rows);
}
void importD2sItem(SessionSnapshot &snapshot, const D2sItem &source, const ClassicData &content, bool hireling) {
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
    item.nativeFlags = source.flags;
    item.nativeFormat = source.format;
    item.nativeGraphic = source.graphic;
    item.nativeHasGraphic = source.hasGraphic;
    item.nativeMaxDurability = source.maxDurability;
    item.nativeQuestDifficulty = source.questDifficulty;
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
        require(found != records.end() && found->code == item.definition, "special item row/base mismatch");
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
            auto found = std::find_if(records.begin(), records.end(), [&](const auto &entry) { return entry.row == index; });
            require(found != records.end(), "unknown affix ID");
            item.affixes.push_back({prefix, int32_t(index), {}});
            item.requiredLevel = std::max(item.requiredLevel, found->requiredLevel);
        }
    if (item.quality == ItemQuality::Rare) {
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
    require(source.durability <= source.maxDurability, "durability exceeds saved maximum");
    ContainerLocation location;
    if (source.mode == 1) {
        require(source.body >= 1 && source.body <= 12, "body slot");
        location = {hireling ? snapshot.containers.hirelingEquipment : snapshot.containers.equipment,
                    {int(source.body - 1), 0}};
        if (!hireling && source.body == 8) {
            location = {snapshot.containers.beltEquipment, {}};
            snapshot.inventory.containers.at(snapshot.containers.belt).spec.rows = definition->beltRows;
        }
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
    snapshot.inventory.items.emplace(item.id, std::move(item));
}
D2sItem exportD2sItem(const SessionSnapshot &snapshot, const ItemInstance &item, const ClassicData &content) {
    const auto *definition = content.items.find(item.definition);
    require(definition != nullptr, "unknown base item");
    D2sItem output;
    output.code = item.definition;
    output.level = item.level;
    output.seed = item.nativeProperties ? item.nativeSeed : uint32_t(item.id.value);
    output.flags = item.nativeProperties ? item.nativeFlags : 0x00800000;
    output.flags = (output.flags & ~0x00080110u) | 0x00800000u | (item.identified ? 0x10u : 0u);
    if (definition->maxDurability && !item.durability) output.flags |= 0x100;
    output.format = item.nativeFormat;
    output.hasGraphic = item.nativeHasGraphic;
    output.graphic = item.nativeGraphic;
    output.questDifficulty = item.nativeProperties ? item.nativeQuestDifficulty : unsigned(snapshot.world.population.difficulty);
    if (content.tables.at(definition->base.sourceTable).number(definition->base.sourceRow, "compactsave").value_or(0))
        output.flags |= 0x00200000;
    output.quality = unsigned(std::find(qualities.begin() + 1, qualities.end(), item.quality) - qualities.begin());
    output.fileIndex = unsigned(std::max(0, item.specialRow >= 0 ? item.specialRow : item.gradeRow));
    size_t prefix = 0, suffix = 0;
    for (const auto &affix : item.affixes) {
        require(affix.row > 0 && (affix.prefix ? prefix : suffix) < 3, "affix index");
        (affix.prefix ? output.prefixes[prefix++] : output.suffixes[suffix++]) = unsigned(affix.row);
    }
    if (item.quality == ItemQuality::Rare) {
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
        const auto resolved = resolveItemStats(content, identified, snapshot.world.player.level);
        std::map<std::pair<int, int>, int64_t> stats;
        for (const auto &stat : resolved) {
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
        output.maxDurability = base ? unsigned(std::clamp<int64_t>(base * (100 + stats[{75, 0}]) / 100 + stats[{73, 0}], 1, 255)) : 0;
        if (item.quality == ItemQuality::Set)
            for (const auto &record : content.setItems) {
                if (int(record.row) != item.specialRow) continue;
                for (const auto &bonus : record.setBonuses) {
                    if (!bonus.perItem || record.setAddFunction != 2) continue;
                    require(bonus.pieces >= 2 && bonus.pieces <= 6 &&
                        (!bonus.property.directRoll || bonus.property.minimum == bonus.property.maximum),
                        "random set bonus has no saved roll");
                    const auto resolved = resolvePropertyStats(content, bonus.property,
                        bonus.property.minimum.value_or(0), snapshot.world.player.level);
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
    const auto *location = std::get_if<ContainerLocation>(&item.location);
    require(location != nullptr, "ground item in character inventory");
    const auto &containers = snapshot.containers;
    if (location->container == containers.equipment || location->container == containers.hirelingEquipment ||
        location->container == containers.beltEquipment) {
        output.mode = 1;
        output.body = location->container == containers.beltEquipment ? 8 : unsigned(location->cell.x + 1);
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
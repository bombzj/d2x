#include "classic_data.hpp"
#include "equipment_data.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
ClassicData loadClassicData(Archives &archives) {
    std::map<std::string, DataTable, std::less<>> tables;
    for (auto name : {"misc", "weapons", "armor", "belts", "monstats", "charstats", "skills"})
        tables.emplace(name, DataTable(archives.read(std::string("data/global/excel/") + name + ".txt")));
    bool legacy = tables.at("monstats").has("Class");
    bool lod = !legacy && tables.at("monstats").has("Id") && tables.at("monstats").has("TreasureClass1");
    if (!legacy && !lod)
        throw std::runtime_error("Unsupported monster table schema");
    const char *treasureName = legacy ? "treasureclass" : "treasureclassex";
    tables.emplace(treasureName,
                   DataTable(archives.read(std::string("data/global/excel/") + treasureName + ".txt")));
    if (lod)
        tables.emplace("itemtypes", DataTable(archives.read("data/global/excel/itemtypes.txt")));
    const auto &tc = tables.at(treasureName);
    if (legacy && (!tc.has("NumCodes") || !tc.has("Code30")))
        throw std::runtime_error("Mixed legacy monster / treasure table schemas");
    if (lod && (!tc.has("Treasure Class") || !tc.has("Prob10") || !tc.has("NoDrop")))
        throw std::runtime_error("Unsupported TreasureClassEx schema");
    std::vector<ItemDefinition> items;
    for (auto [name, family] : {std::pair{"misc", ItemFamily::Misc},
                                {"weapons", ItemFamily::Weapon},
                                {"armor", ItemFamily::Armor}}) {
        const auto &table = tables.at(name);
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto number = [&](std::string_view key) { return table.number(row, key); };
            auto value = [&](std::string_view key) { return std::string(table.value(row, key)); };
            if (value("code").empty())
                continue;
            ItemDefinition item;
            item.code = value("code");
            item.name = value("name");
            item.family = family;
            item.width = number("invwidth").value_or(0);
            item.height = number("invheight").value_or(0);
            item.maxStack = number("stackable").value_or(0) ? std::max(1, number("maxstack").value_or(1)) : 1;
            item.maxDurability = std::max(0, number("durability").value_or(0));
            item.beltAllowed = family == ItemFamily::Misc && number("belt").value_or(0) != 0;
            item.usable = number("useable").value_or(0) != 0;
            item.autoBelt = number("autobelt").value_or(0) != 0;
            if (!value("invfile").empty())
                item.icon = "data/global/items/" + value("invfile") + ".dc6";
            if (!value("flippyfile").empty())
                item.groundAnimation = "data/global/items/" + value("flippyfile") + ".dc6";
            auto &base = item.base;
            base.type = value("type");
            base.secondaryType = value("type2");
            base.weaponClass = value("wclass");
            base.minDamage = number("mindam");
            base.maxDamage = number("maxdam");
            base.twoHandMin = number("2handmindam");
            base.twoHandMax = number("2handmaxdam");
            base.throwMin = number("minmisdam");
            base.throwMax = number("maxmisdam");
            base.minDefense = number("minac");
            base.maxDefense = number("maxac");
            base.requiredStrength = number("reqstr");
            base.requiredDexterity = number("reqdex");
            base.requiredLevel = number("levelreq");
            base.level = number("level");
            base.cost = number("cost");
            base.speed = number("speed");
            base.strengthBonus = number("strbonus");
            base.dexterityBonus = number("dexbonus");
            base.block = number("block");
            base.sockets = number("gemsockets");
            base.rarity = number("rarity");
            base.spawnable = number("spawnable");
            base.sourceTable = name;
            base.sourceRow = row;
            // In this schema type is a numeric engine type ID, not a LoD ItemTypes code.
            bool numericType = !base.type.empty() && std::all_of(base.type.begin(), base.type.end(),
                                                                 [](char c) { return c >= '0' && c <= '9'; });
            if (base.type.empty() || numericType != legacy)
                throw std::runtime_error("Mixed item data schema: " + item.code);
            if (lod && base.type == "belt") {
                auto shape = number("belt");
                const auto &belts = tables.at("belts");
                if (!shape || *shape < 0 || size_t(*shape) >= belts.rows().size())
                    throw std::runtime_error("Invalid original belt index: " + item.code);
                item.beltRows = belts.number(*shape, "numboxes").value_or(0) / 4;
                if (item.beltRows < 1 || item.beltRows > 4)
                    throw std::runtime_error("Invalid original belt capacity");
            }
            items.push_back(std::move(item));
        }
    }
    // 1.04 armor.belt is zero even for belts. Keep the version adapter explicit;
    // capacities themselves come from the named rows of the original belts.txt.
    const auto &belts = tables.at("belts");
    for (auto &item : items) {
        if (!legacy)
            break;
        std::string_view shape;
        if (item.code == "lbl")
            shape = "sash";
        if (item.code == "vbl")
            shape = "light belt";
        if (item.code == "mbl")
            shape = "belt";
        if (item.code == "tbl")
            shape = "heavy belt";
        if (item.code == "hbl")
            shape = "girdle";
        if (item.code == "zlb" || item.code == "zvb" || item.code == "zmb" || item.code == "ztb" ||
            item.code == "zhb")
            shape = "uber belt";
        if (shape.empty())
            continue;
        for (size_t row = 0; row < belts.rows().size(); ++row)
            if (belts.value(row, "name") == shape)
                item.beltRows = belts.number(row, "numboxes").value_or(0) / 4;
        if (item.beltRows < 1 || item.beltRows > 4)
            throw std::runtime_error("Missing original belt layout: " + std::string(shape));
    }
    if (lod)
        loadEquipmentDefinitions(items, tables.at("itemtypes"), tables);
    ClassicData data{ItemCatalog(std::move(items)),
                     std::move(tables),
                     {},
                     {},
                     legacy ? "classic-1.04-txt-v1" : "lod-named-txt-v1"};
    if (lod) {
        loadLodTreasureData(data);
        return data;
    }
    const auto &treasureTable = data.tables.at("treasureclass");
    for (size_t row = 0; row < treasureTable.rows().size(); ++row) {
        ClassicTreasureClass treasure;
        treasure.name = treasureTable.value(row, "TreasureClass");
        int count = treasureTable.number(row, "NumCodes").value_or(-1);
        if (count < 0 || count > 30 || treasure.name.empty())
            throw std::runtime_error("Invalid classic TreasureClass row");
        for (int slot = 1; slot <= count; ++slot) {
            auto code = treasureTable.value(row, "Code" + std::to_string(slot));
            if (code.empty())
                throw std::runtime_error("Empty classic treasure slot");
            treasure.codes.emplace_back(code);
        }
        data.treasures.push_back(std::move(treasure));
    }
    const auto &monsters = data.tables.at("monstats");
    for (size_t row = 0; row < monsters.rows().size(); ++row) {
        ClassicMonsterData monster;
        monster.name = monsters.value(row, "Class");
        monster.token = monsters.value(row, "Code");
        const char *suffix[] = {"", "(N)", "(H)"};
        for (int difficulty = 0; difficulty < 3; ++difficulty)
            for (int slot = 0; slot < 4; ++slot) {
                int index =
                    monsters.number(row, "TreasureClass" + std::to_string(slot + 1) + suffix[difficulty])
                        .value_or(0);
                if (index < 0 || size_t(index) >= data.treasures.size())
                    throw std::runtime_error("Unresolved monster TreasureClass: " + monster.name);
                monster.treasureClasses[difficulty][slot] = unsigned(index);
            }
        data.monsters.push_back(std::move(monster));
    }
    return data;
}
} // namespace d2x

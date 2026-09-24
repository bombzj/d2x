#include "classic_data.hpp"
#include "character_attributes.hpp"
#include "character_progression.hpp"
#include "equipment_data.hpp"
#include "item_appearance.hpp"
#include "item_affixes.hpp"
#include "item_properties.hpp"
#include "item_projectiles.hpp"
#include "item_consumables.hpp"
#include "item_grades.hpp"
#include "special_items.hpp"
#include "sorceress_data.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
ClassicData loadClassicData(Archives &archives) {
    std::map<std::string, DataTable, std::less<>> tables;
    for (auto name : {"misc", "weapons", "armor", "armtype", "belts", "monstats", "charstats", "skills", "experience", "inventory"})
        tables.emplace(name, DataTable(archives.read(std::string("data/global/excel/") + name + ".txt")));
    const auto &armtype = tables.at("armtype");
    if (!armtype.has("Token"))
        throw std::runtime_error("ArmType lacks original Token column");
    std::vector<std::string> armorTypes;
    for (size_t row = 0; row < armtype.rows().size(); ++row)
        armorTypes.emplace_back(armtype.value(row, "Token"));
    if (armorTypes.empty() || armorTypes.front().empty())
        throw std::runtime_error("ArmType lacks the unarmored appearance token");
    bool legacy = tables.at("monstats").has("Class");
    bool lod = !legacy && tables.at("monstats").has("Id") && tables.at("monstats").has("TreasureClass1");
    if (!legacy && !lod)
        throw std::runtime_error("Unsupported monster table schema");
    const char *treasureName = legacy ? "treasureclass" : "treasureclassex";
    tables.emplace(treasureName,
                   DataTable(archives.read(std::string("data/global/excel/") + treasureName + ".txt")));
    if (lod) {
        tables.emplace("books", DataTable(archives.read("data/global/excel/books.txt")));
        tables.emplace("shrines", DataTable(archives.read("data/global/excel/shrines.txt")));
        tables.emplace("monlvl", DataTable(archives.read("data/global/excel/monlvl.txt")));
        tables.emplace("missiles", DataTable(archives.read("data/global/excel/missiles.txt")));
        if (archives.contains("data/global/excel/npc.txt"))
            tables.emplace("npc", DataTable(archives.read("data/global/excel/npc.txt")));
        tables.emplace("itemtypes", DataTable(archives.read("data/global/excel/itemtypes.txt")));
        tables.emplace("storepage", DataTable(archives.read("data/global/excel/storepage.txt")));
        tables.emplace("hireling", DataTable(archives.read("data/global/excel/hireling.txt")));
        if (archives.contains("data/global/excel/itemratio.txt"))
            tables.emplace("itemratio", DataTable(archives.read("data/global/excel/itemratio.txt")));
        for (auto name : {"uniqueitems", "setitems", "sets", "magicprefix", "magicsuffix",
                          "rareprefix", "raresuffix",
                          "properties", "itemstatcost", "qualityitems", "lowqualityitems"})
            if (archives.contains(std::string("data/global/excel/") + name + ".txt"))
                tables.emplace(name, DataTable(archives.read(std::string("data/global/excel/") + name + ".txt")));
    }
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
            item.opensCube = lod && family == ItemFamily::Misc &&
                             value("type") == "ques" && number("pSpell") == 7;
            item.autoBelt = number("autobelt").value_or(0) != 0;
            item.imbueable = (number("bitfield1").value_or(0) & 1) != 0 &&
                             number("quest").value_or(0) == 0;
            if (!value("invfile").empty())
                item.icon = "data/global/items/" + value("invfile") + ".dc6";
            if (!value("flippyfile").empty())
                item.groundAnimation = "data/global/items/" + value("flippyfile") + ".dc6";
            item.artAvailable = !item.icon.empty() && !item.groundAnimation.empty() &&
                                archives.contains(item.icon) && archives.contains(item.groundAnimation);
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
            if (auto id = number("missiletype"); id && *id >= 0)
                base.projectile = ItemBaseStats::Projectile{*id};
            base.minDefense = number("minac");
            base.maxDefense = number("maxac");
            base.requiredStrength = number("reqstr");
            base.requiredDexterity = number("reqdex");
            base.requiredLevel = number("levelreq");
            base.level = number("level");
            base.magicLevel = number("magic lvl");
            base.cost = number("cost");
            base.speed = number("speed");
            base.strengthBonus = number("strbonus");
            base.dexterityBonus = number("dexbonus");
            base.block = number("block");
            base.lightRadius = number("lightradius");
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
    if (lod) {
        const auto &books = tables.at("books");
        const auto &misc = tables.at("misc");
        for (auto &item : items) {
            if (item.base.type != "book") continue;
            for (size_t row = 0; row < books.rows().size(); ++row) {
                if (books.value(row, "BookSpellCode") != item.code) continue;
                item.bookScroll = std::string(books.value(row, "ScrollSpellCode"));
                item.bookCapacity = item.maxStack;
                item.bookChargeCost = unsigned(std::max(0, books.number(row, "CostPerCharge").value_or(0)));
                item.bookInitialCharges = unsigned(std::max(0, misc.number(item.base.sourceRow, "spawnstack").value_or(0)));
                if (item.bookScroll.empty() || item.bookCapacity < 1 ||
                    item.bookInitialCharges > item.bookCapacity)
                    throw std::runtime_error("Invalid original book data: " + item.code);
                item.maxStack = 1;
                break;
            }
            if (item.bookScroll.empty())
                throw std::runtime_error("Missing original books.txt mapping: " + item.code);
        }
    }
    loadItemAppearances(items, tables, armorTypes);
    if (lod)
        loadEquipmentDefinitions(items, tables.at("itemtypes"), tables);
    if (lod)
        loadItemProjectiles(items, tables.at("missiles"), archives);
    ClassicData data{ItemCatalog(std::move(items)), std::move(tables),
                     legacy ? "classic-1.04-txt-v1" : "lod-named-txt-v1"};
    const auto &inventory = data.tables.at("inventory");
    const auto stashName = lod ? "Big Bank Page 1" : "Bank Page 1";
    size_t stashRow = 0;
    for (; stashRow < inventory.rows().size(); ++stashRow)
        if (inventory.value(stashRow, "class") == stashName) break;
    if (stashRow == inventory.rows().size())
        throw std::runtime_error("MPQ inventory.txt lacks the original stash layout");
    auto requiredGrid = [&](std::string_view field) {
        auto value = inventory.number(stashRow, field);
        if (!value) throw std::runtime_error("Invalid MPQ stash layout field: " + std::string(field));
        return *value;
    };
    data.stashLayout = {requiredGrid("gridX"), requiredGrid("gridY"),
                        requiredGrid("gridLeft"), requiredGrid("gridTop"),
                        requiredGrid("gridBoxWidth"), lod};
    if (data.stashLayout.columns < 1 || data.stashLayout.rows < 1 ||
        data.stashLayout.columns > 16 || data.stashLayout.rows > 16 ||
        data.stashLayout.left < 0 || data.stashLayout.top < 0 ||
        data.stashLayout.cellSize < 1 || data.stashLayout.cellSize != requiredGrid("gridBoxHeight") ||
        data.stashLayout.left + data.stashLayout.columns * data.stashLayout.cellSize > 320 ||
        data.stashLayout.top + data.stashLayout.rows * data.stashLayout.cellSize > 432)
        throw std::runtime_error("Unsupported MPQ stash grid geometry");
    if (lod) {
        size_t cubeRow = 0;
        for (; cubeRow < inventory.rows().size(); ++cubeRow)
            if (inventory.value(cubeRow, "class") == "Transmogrify Box Page 1") break;
        if (cubeRow == inventory.rows().size())
            throw std::runtime_error("MPQ inventory.txt lacks the cube layout");
        auto cubeNumber = [&](std::string_view field) {
            auto value = inventory.number(cubeRow, field);
            if (!value) throw std::runtime_error("Invalid MPQ cube layout field: " + std::string(field));
            return *value;
        };
        data.cubeLayout = {cubeNumber("gridX"), cubeNumber("gridY"),
                           cubeNumber("gridLeft"), cubeNumber("gridTop"),
                           cubeNumber("gridBoxWidth"), true};
        if (data.cubeLayout.columns < 1 || data.cubeLayout.rows < 1 ||
            data.cubeLayout.cellSize != cubeNumber("gridBoxHeight") ||
            data.cubeLayout.left < 0 || data.cubeLayout.top < 0 ||
            data.cubeLayout.left + data.cubeLayout.columns * data.cubeLayout.cellSize > 320 ||
            data.cubeLayout.top + data.cubeLayout.rows * data.cubeLayout.cellSize > 432)
            throw std::runtime_error("Unsupported MPQ cube grid geometry");
        const auto &misc = data.tables.at("misc");
        for (size_t row = 0; row < misc.rows().size(); ++row)
            if (misc.number(row, "pSpell") == 7 && misc.value(row, "type") == "ques") {
                if (!data.cubeCode.empty()) throw std::runtime_error("Ambiguous original cube item");
                data.cubeCode = std::string(misc.value(row, "code"));
            }
        if (data.cubeCode.empty() || !data.items.find(data.cubeCode))
            throw std::runtime_error("MPQ lacks the original cube item");
    }
    data.characters = loadCharacterDefinitions(data.tables.at("charstats"));
    if (lod)
        data.hirelings = loadHirelingDefinitions(data.tables.at("hireling"));
    if (lod) {
        data.tables.emplace("skilldesc", DataTable(archives.read("data/global/excel/skilldesc.txt")));
        ClassicStrings strings(archives);
        for (const auto &[key, value] : strings.entries())
            if (key.starts_with("qstsa1q") || key == "newquestlog" ||
                key == "qstsComplete" || key == "noactivequest")
                data.actOneQuestStrings.emplace(key, value);
            else if (key.starts_with("merc"))
                data.hirelingStrings.emplace(key, value);
        data.skills = loadSkillCatalog(data.tables.at("skills"), data.tables.at("skilldesc"),
                                       data.tables.at("charstats"), data.characters, strings);
        const DataTable overlays(archives.read("data/global/excel/overlay.txt"));
        const DataTable sounds(archives.read("data/global/excel/sounds.txt"));
        loadSorceressEffects(data.skills, data.tables.at("skills"), data.tables.at("missiles"),
                             overlays, sounds, archives);
        const DataTable levels(archives.read("data/global/excel/levels.txt"));
        for (size_t row = 0; row < levels.rows().size(); ++row)
            if (auto id = levels.number(row, "Id"); id && *id > 0)
                if (auto allowed = levels.number(row, "Teleport"))
                    data.teleportByLevel.emplace(*id, *allowed);
        const DataTable difficulties(archives.read("data/global/excel/difficultylevels.txt"));
        if (difficulties.rows().size() < data.staticFieldMinimum.size())
            throw std::runtime_error("Missing original Static Field difficulty limits");
        for (size_t index = 0; index < data.staticFieldMinimum.size(); ++index) {
            auto value = difficulties.number(index, "StaticFieldMin");
            auto penalty = difficulties.number(index, "ResistPenalty");
            if (!value || *value < 0 || *value > 100)
                throw std::runtime_error("Invalid original Static Field difficulty limit");
            if (!penalty || *penalty < -200 || *penalty > 100)
                throw std::runtime_error("Invalid original resistance difficulty penalty");
            data.staticFieldMinimum[index] = *value;
            data.resistancePenalty[index] = *penalty;
        }
    }
    for (const auto &character : data.characters)
        data.experienceByClass.emplace(character.name,
            experienceThresholds(data.tables.at("experience"), character.name));
    data.armorTypes = std::move(armorTypes);
    data.vendors = loadVendorData(data.tables, data.items);
    if (archives.contains("data/local/docs/eng/a1npc.txt"))
        data.npcDialogues = loadActOneNpcDialogues(archives);
    loadItemConsumables(data);
    if (lod) {
        loadPropertyData(data);
        loadItemGrades(data);
        loadSpecialItemData(data);
        auto resolveSpecialArt = [&](SpecialItemRecord &record) {
            const auto *base = data.items.find(record.code);
            if (!base)
                return;
            const auto &icon = record.icon.empty() ? base->icon : record.icon;
            const auto &ground = record.groundAnimation.empty() ? base->groundAnimation
                                                                 : record.groundAnimation;
            record.artAvailable = !icon.empty() && !ground.empty() &&
                                  archives.contains(icon) && archives.contains(ground);
        };
        for (auto &record : data.uniqueItems) {
            resolveSpecialArt(record);
        }
        for (auto &record : data.setItems) {
            resolveSpecialArt(record);
        }
        loadMagicAffixData(data);
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

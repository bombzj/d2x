#include "resources/archive.hpp"
#include "classic_data.hpp"
#include "content/character/character_attributes.hpp"
#include "content/character/character_progression.hpp"
#include "content/items/equipment_data.hpp"
#include "content/items/item_appearance.hpp"
#include "content/items/item_affixes.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/item_projectiles.hpp"
#include "content/skills/skill_animation.hpp"
#include "content/items/item_consumables.hpp"
#include "content/items/item_grades.hpp"
#include "content/items/special_items.hpp"
#include "content/skills/sorceress_data.hpp"
#include "content/skills/weapon_skill_data.hpp"
#include "content/skills/necromancer_data.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace d2x {
ClassicData loadClassicData(Archives &archives) {
    const ClassicStrings strings(archives);
    std::map<std::string, DataTable, std::less<>> tables;
    for (auto name : {"monumod", "monstats2", "montype", "difficultylevels", "misc", "weapons", "armor", "armtype", "belts", "monstats", "charstats", "skills", "experience", "inventory", "levels"})
        tables.emplace(name, DataTable(archives.read(std::string("data/global/excel/") + name + ".txt")));
    const auto &armtype = tables.at("armtype");
    if (!armtype.has("Token"))
        throw std::runtime_error("ArmType lacks original Token column");
    std::vector<std::string> armorTypes;
    for (size_t row = 0; row < armtype.rows().size(); ++row)
        armorTypes.emplace_back(armtype.value(row, "Token"));
    if (armorTypes.empty() || armorTypes.front().empty())
        throw std::runtime_error("ArmType lacks the unarmored appearance token");
    if (!tables.at("monstats").has("Id") || !tables.at("monstats").has("TreasureClass1") ||
        !archives.contains("data/global/ui/panel/invchar6.dc6"))
        throw std::runtime_error("Lord of Destruction expansion MPQs are required");
    const char *treasureName = "treasureclassex";
    tables.emplace(treasureName,
                   DataTable(archives.read(std::string("data/global/excel/") + treasureName + ".txt")));
    {
        tables.emplace("books", DataTable(archives.read("data/global/excel/books.txt")));
        tables.emplace("shrines", DataTable(archives.read("data/global/excel/shrines.txt")));
        tables.emplace("monlvl", DataTable(archives.read("data/global/excel/monlvl.txt")));
        tables.emplace("missiles", DataTable(archives.read("data/global/excel/missiles.txt")));
        if (archives.contains("data/global/excel/npc.txt"))
            tables.emplace("npc", DataTable(archives.read("data/global/excel/npc.txt")));
        tables.emplace("itemtypes", DataTable(archives.read("data/global/excel/itemtypes.txt")));
        tables.emplace("storepage", DataTable(archives.read("data/global/excel/storepage.txt")));
        tables.emplace("gamble", DataTable(archives.read("data/global/excel/gamble.txt")));
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
    if (!tc.has("Treasure Class") || !tc.has("Prob10") || !tc.has("NoDrop"))
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
            const auto nameKey = value("namestr");
            const auto displayName = strings.find(nameKey.empty() ? item.code : nameKey);
            item.name = displayName.empty() ? value("name") : std::string(displayName);
            item.family = family;
            if (family == ItemFamily::Misc) item.betterGem = value("bettergem");
            item.width = number("invwidth").value_or(0);
            item.height = number("invheight").value_or(0);
            item.maxStack = number("stackable").value_or(0) ? std::max(1, number("maxstack").value_or(1)) : 1;
            item.maxDurability = std::max(0, number("durability").value_or(0));
            item.beltAllowed = family == ItemFamily::Misc && number("belt").value_or(0) != 0;
            item.usable = number("useable").value_or(0) != 0;
            item.targetCursor = number("spellicon").value_or(-1);
            item.opensCube = family == ItemFamily::Misc &&
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
            if (auto id = number("missiletype"); id && *id >= 0) {
                base.projectile.emplace();
                base.projectile->id = *id;
            }
            base.minDefense = number("minac");
            base.maxDefense = number("maxac");
            base.requiredStrength = number("reqstr");
            base.requiredDexterity = number("reqdex");
            base.requiredLevel = number("levelreq");
            base.level = number("level");
            base.magicLevel = number("magic lvl");
            base.cost = number("cost");
            base.speed = number("speed");
            base.rangeAdder = number("rangeadder").value_or(0);
            base.strengthBonus = number("strbonus");
            base.dexterityBonus = number("dexbonus");
            base.block = number("block");
            base.lightRadius = number("lightradius");
            base.sockets = number("gemsockets");
            base.rarity = number("rarity");
            base.spawnable = number("spawnable");
            base.sourceTable = name;
            base.sourceRow = row;
            // Expansion tables refer to ItemTypes by code, never by legacy numeric IDs.
            bool numericType = !base.type.empty() && std::all_of(base.type.begin(), base.type.end(),
                                                                 [](char c) { return c >= '0' && c <= '9'; });
            if (base.type.empty() || numericType)
                throw std::runtime_error("Mixed item data schema: " + item.code);
            if (base.type == "belt") {
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
    {
        const auto &books = tables.at("books");
        const auto &misc = tables.at("misc");
        for (auto &item : items) {
            // ITEMS_GetSpellIcon uses Books for both scrolls and tomes, overriding
            // Misc.spellicon (which is -1 for the original identify items).
            for (size_t row = 0; row < books.rows().size(); ++row)
                if (books.value(row, "ScrollSpellCode") == item.code ||
                    books.value(row, "BookSpellCode") == item.code)
                    item.targetCursor = books.number(row, "SpellIcon").value_or(-1);
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
    loadEquipmentDefinitions(items, tables.at("itemtypes"), tables);
    for (auto &item : items)
        if (std::any_of(item.inventoryIcons.begin(), item.inventoryIcons.end(),
                        [&](const auto &icon) { return !archives.contains(icon); }))
            item.artAvailable = false;
    loadItemProjectiles(items, tables.at("missiles"), archives);
    ClassicData data{ItemCatalog(std::move(items)), std::move(tables),
                     "lod-named-txt-v1"};
    const auto &inventory = data.tables.at("inventory");
    const auto stashName = "Big Bank Page 1";
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
                        requiredGrid("gridBoxWidth"), true};
    if (data.stashLayout.columns < 1 || data.stashLayout.rows < 1 ||
        data.stashLayout.columns > 16 || data.stashLayout.rows > 16 ||
        data.stashLayout.left < 0 || data.stashLayout.top < 0 ||
        data.stashLayout.cellSize < 1 || data.stashLayout.cellSize != requiredGrid("gridBoxHeight") ||
        data.stashLayout.left + data.stashLayout.columns * data.stashLayout.cellSize > 320 ||
        data.stashLayout.top + data.stashLayout.rows * data.stashLayout.cellSize > 432)
        throw std::runtime_error("Unsupported MPQ stash grid geometry");
    {
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
    // D2MOO MissMode.cpp's mode table. Unit bits are retained in the definition;
    // Grid supplies terrain/object flags, while combat resolves unit hits.
    const auto &missiles = data.tables.at("missiles");
    constexpr uint16_t collisionMasks[]{0, 0x0084, 0x0104, 0x0184, 0, 0x0104, 0x0004, 0x0040, 0x0185};
    for (size_t row = 0; row < missiles.rows().size(); ++row) {
        const auto id = missiles.number(row, "Id");
        if (!id) continue;
        const int mode = missiles.number(row, "CollideType").value_or(0);
        const int size = missiles.number(row, "Size").value_or(0);
        if (mode < 0 || mode >= int(std::size(collisionMasks)) || size < 0 || size > 3)
            throw std::runtime_error("Unsupported original missile collision: " + std::to_string(*id));
        data.missileCollisions.emplace(*id, MissileCollisionRule{collisionMasks[mode], size});
        const int returnFire = missiles.number(row, "ReturnFire").value_or(0);
        if (returnFire != 0 && returnFire != 1)
            throw std::runtime_error("Unsupported original missile ReturnFire: " + std::to_string(*id));
        data.missileReturnFire.emplace(*id, returnFire == 1);
    }
    data.characters = loadCharacterDefinitions(data.tables.at("charstats"));
    data.hirelings = loadHirelingDefinitions(data.tables.at("hireling"));
    {
        data.tables.emplace("skilldesc", DataTable(archives.read("data/global/excel/skilldesc.txt")));
        data.hirelingLayout = loadHirelingLayout(data.tables.at("inventory"));
        data.itemStrings = strings.entries();
        const DataTable hireDescriptions(archives.read("data/global/excel/hiredesc.txt"));
        for (size_t row = 0; row < hireDescriptions.rows().size(); ++row) {
            const auto code = hireDescriptions.value(row, "Code");
            const auto label = hireDescriptions.value(row, "Hireling Description");
            if (!code.empty()) data.hirelingDescriptions.emplace(code, label);
        }
        for (const auto &[key, value] : strings.entries())
            if (key.starts_with("qstsa1q") || key == "newquestlog" ||
                key == "qstsComplete" || key == "noactivequest")
                data.actOneQuestStrings.emplace(key, value);
            else if (key.starts_with("merc"))
                data.hirelingStrings.emplace(key, value);
        data.skills = loadSkillCatalog(data.tables.at("skills"), data.tables.at("skilldesc"),
                                       data.tables.at("charstats"), data.characters, strings);
        loadSkillAnimations(data.skills, data.tables.at("weapons"), archives);
        data.shrines = loadShrines(data.tables.at("shrines"));
        data.states = loadCombatStates(DataTable(archives.read("data/global/excel/states.txt")));
        loadMonsterEnchantmentResources(data, archives);
        const DataTable overlays(archives.read("data/global/excel/overlay.txt"));
        const DataTable sounds(archives.read("data/global/excel/sounds.txt"));
        loadSorceressEffects(data.skills, data.tables.at("skills"), data.tables.at("missiles"),
                             overlays, sounds, data.states, archives);
        loadWeaponSkills(data.skills, data.tables.at("skills"), data.tables.at("missiles"), sounds, archives);
        loadNecromancerSummons(data.skills, data.tables.at("skills"), data.tables.at("monstats"),
            data.tables.at("monstats2"), data.tables.at("monlvl"), sounds, archives);
        const DataTable levels(archives.read("data/global/excel/levels.txt"));
        for (size_t row = 0; row < levels.rows().size(); ++row)
            if (auto id = levels.number(row, "Id"); id && *id > 0)
                if (auto allowed = levels.number(row, "Teleport"))
                    data.teleportByLevel.emplace(*id, *allowed);
        const auto &difficulties = data.tables.at("difficultylevels");
        if (difficulties.rows().size() < data.staticFieldMinimum.size())
            throw std::runtime_error("Missing original Static Field difficulty limits");
        for (size_t index = 0; index < data.staticFieldMinimum.size(); ++index) {
            auto value = difficulties.number(index, "StaticFieldMin");
            auto penalty = difficulties.number(index, "ResistPenalty");
            auto freezeDivisor = difficulties.number(index, "MonsterFreezeDivisor");
            auto coldDivisor = difficulties.number(index, "MonsterColdDivisor");
            if (!value || *value < 0 || *value > 100)
                throw std::runtime_error("Invalid original Static Field difficulty limit");
            if (!penalty || *penalty < -200 || *penalty > 100)
                throw std::runtime_error("Invalid original resistance difficulty penalty");
            if (!freezeDivisor || *freezeDivisor <= 0)
                throw std::runtime_error("Invalid original monster freeze divisor");
            if (!coldDivisor || *coldDivisor <= 0)
                throw std::runtime_error("Invalid original monster cold divisor");
            data.staticFieldMinimum[index] = *value;
            data.monsterFreezeDivisor[index] = *freezeDivisor;
            data.monsterColdDivisor[index] = *coldDivisor;
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
    {
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
}
} // namespace d2x

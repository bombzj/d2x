#include "content/skills/description_data.hpp"
#include "content/skills/formula_data.hpp"
#include "content/monsters/mode_stat_data.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include "classic_data.hpp"
#include "monsters/monster_type_data.hpp"
#include "content/character/character_attributes.hpp"
#include "content/character/character_progression.hpp"
#include "content/items/equipment_data.hpp"
#include "content/items/equipment_modifiers.hpp"
#include "content/items/item_appearance.hpp"
#include "content/items/item_affixes.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/socket_data.hpp"
#include "content/items/cube_data.hpp"
#include "content/items/item_projectiles.hpp"
#include "content/skills/skill_animation.hpp"
#include "content/items/item_consumables.hpp"
#include "content/items/item_grades.hpp"
#include "content/items/item_display.hpp"
#include "content/items/special_items.hpp"
#include "content/skills/sorceress_data.hpp"
#include "content/skills/defensive_buff_data.hpp"
#include "content/skills/weapon_skill_data.hpp"
#include "content/skills/amazon_bow_data.hpp"
#include "content/skills/amazon_spear_data.hpp"
#include "content/skills/amazon_magic_data.hpp"
#include "content/skills/amazon_summon_data.hpp"
#include "content/skills/necromancer_data.hpp"
#include "content/skills/necro_summon_data.hpp"
#include "content/skills/curse_data.hpp"
#include "content/skills/bone_data.hpp"
#include "content/skills/barbarian_data.hpp"
#include "content/skills/assassin_data.hpp"
#include "content/skills/druid_data.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include "content/skills/aura_data.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace d2x {
std::shared_ptr<const ClassicData> sharedClassicData(Archives &archives, ContentPreparation preparation) {
    if (preparation == ContentPreparation::OriginalRules)
        return archives.prepared<ClassicData>([&] { return loadClassicData(archives); });
    struct RemoteDisplayData { std::shared_ptr<const ClassicData> value; };
    return archives.prepared<RemoteDisplayData>([&] {
        return RemoteDisplayData{std::make_shared<const ClassicData>(loadClassicData(archives, preparation))};
    })->value;
}
ClassicData loadClassicData(Archives &archives, ContentPreparation preparation) {
    const bool originalRules = preparation == ContentPreparation::OriginalRules;
    const ClassicStrings strings(archives);
    std::map<std::string, DataTable, std::less<>> tables;
    for (auto name : {"monumod", "monstats2", "monprop", "monequip", "bodylocs", "montype", "difficultylevels", "misc", "weapons", "armor", "armtype", "belts", "monstats", "charstats", "skills", "experience", "inventory", "levels", "hitclass", "pettype", "objects"})
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
        tables.emplace("cubemain", DataTable(archives.read("data/global/excel/cubemain.txt")));
        tables.emplace("gems", DataTable(archives.read("data/global/excel/gems.txt")));
        tables.emplace("runes", DataTable(archives.read("data/global/excel/runes.txt")));
        tables.emplace("books", DataTable(archives.read("data/global/excel/books.txt")));
        tables.emplace("shrines", DataTable(archives.read("data/global/excel/shrines.txt")));
        tables.emplace("monlvl", DataTable(archives.read("data/global/excel/monlvl.txt")));
        tables.emplace("missiles", DataTable(archives.read("data/global/excel/missiles.txt")));
        if (archives.contains("data/global/excel/npc.txt"))
            tables.emplace("npc", DataTable(archives.read("data/global/excel/npc.txt")));
        tables.emplace("itemtypes", DataTable(archives.read("data/global/excel/itemtypes.txt")));
        tables.emplace("elemtypes", DataTable(archives.read("data/global/excel/elemtypes.txt")));
        tables.emplace("events", DataTable(archives.read("data/global/excel/events.txt")));
        tables.emplace("storepage", DataTable(archives.read("data/global/excel/storepage.txt")));
        tables.emplace("gamble", DataTable(archives.read("data/global/excel/gamble.txt")));
        tables.emplace("hireling", DataTable(archives.read("data/global/excel/hireling.txt")));
        if (archives.contains("data/global/excel/itemratio.txt"))
            tables.emplace("itemratio", DataTable(archives.read("data/global/excel/itemratio.txt")));
        for (auto name : {"uniqueitems", "setitems", "sets", "magicprefix", "magicsuffix",
                          "rareprefix", "raresuffix", "automagic",
                          "properties", "itemstatcost", "qualityitems", "lowqualityitems", "colors"})
            if (archives.contains(std::string("data/global/excel/") + name + ".txt"))
                tables.emplace(name, DataTable(archives.read(std::string("data/global/excel/") + name + ".txt")));
    }
    const auto &tc = tables.at(treasureName);
    if (!tc.has("Treasure Class") || !tc.has("Prob10") || !tc.has("NoDrop"))
        throw std::runtime_error("Unsupported TreasureClassEx schema");
    std::vector<ItemDefinition> items;
    std::map<std::string, int, std::less<>> hitClasses;
    const auto &hitClassTable = tables.at("hitclass");
    for (size_t row = 0; row < hitClassTable.rows().size(); ++row) {
        const auto code = hitClassTable.value(row, "code");
        if (!code.empty()) hitClasses.emplace(std::string(code), int(row));
    }
    for (auto [name, family] : {std::pair{"misc", ItemFamily::Misc},
                                {"weapons", ItemFamily::Weapon},
                                {"armor", ItemFamily::Armor}}) {
        const auto &table = tables.at(name);
        if (!table.has("quest"))
            throw std::runtime_error("Item base table lacks original quest column: " + std::string(name));
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
            if (item.code == "toa")
                if (const auto separator = item.name.rfind('\n'); separator != std::string::npos)
                    item.name.erase(0, separator + 1);
            item.family = family;
            item.questTag = number("quest").value_or(0);
            item.gemApplyType = number("gemapplytype").value_or(-1);
            item.personalizable = number("nameable").value_or(0) != 0 && !number("compactsave").value_or(0);
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
            item.ironGolemMaterial = (number("bitfield1").value_or(0)&2)!=0;
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
            if (family == ItemFamily::Weapon) {
                const auto code = value("hit class");
                if (!code.empty()) {
                    const auto found = hitClasses.find(code);
                    if (found == hitClasses.end()) throw std::runtime_error("Unknown original weapon hit class: " + code);
                    base.hitClass = found->second;
                }
            }
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
            const auto &types = tables.at("itemtypes");
            for (size_t type = 0; type < types.rows().size(); ++type)
                if (types.value(type, "Code") == base.type)
                    for (size_t tier = 0; tier < base.socketsByLevel.size(); ++tier)
                        base.socketsByLevel[tier] = std::min(base.sockets.value_or(0),
                            types.number(type, std::array{"MaxSock1", "MaxSock25", "MaxSock40"}[tier]).value_or(0));
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
    loadQuestItemCarryRules(items);
    for (auto &item : items)
        if (std::any_of(item.inventoryIcons.begin(), item.inventoryIcons.end(),
                        [&](const auto &icon) { return !archives.contains(icon); }))
            item.artAvailable = false;
    if (originalRules) loadItemProjectiles(items, tables.at("missiles"), archives);
    ClassicData data{ItemCatalog(std::move(items)), std::move(tables),
                     originalRules ? "lod-named-txt-v1" : "remote-display-txt-v1"};
    {
        const auto bytes=archives.read("data/global/expfield.d2");Reader reader(bytes);reader.skip(2);
        const auto height=reader.u32(),width=reader.u32();
        if(width!=256 || !height || height>65536 || uint64_t(width)*height!=bytes.size()-reader.pos)
            throw std::runtime_error("Invalid original collision direction field");
        auto field=std::make_shared<CollisionField>();field->width=int(width);field->height=int(height);
        field->directions.assign(bytes.begin()+reader.pos,bytes.end());
        if(std::any_of(field->directions.begin(),field->directions.end(),[](uint8_t value){return value>8;}) ||
            field->directions[size_t(height/2)*width+width/2]!=8)
            throw std::runtime_error("Invalid original collision field directions");
        data.nativeCollisionField=std::move(field);
    }
    const auto &inventory = data.tables.at("inventory");
    if (originalRules) {
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
    }
    // D2MOO MissMode.cpp's mode table. Unit bits are retained in the definition;
    // Grid supplies terrain/object flags, while combat resolves unit hits.
    const auto &missiles = data.tables.at("missiles");
    constexpr uint16_t collisionMasks[]{0, 0x0084, 0x0104, 0x0184, 0, 0x0104, 0x0004, 0x0040, 0x0185};
    if (originalRules) for (size_t row = 0; row < missiles.rows().size(); ++row) {
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
    for (const auto &character : data.characters) {
        size_t row = 0;
        for (; row < inventory.rows().size(); ++row)
            if (inventory.value(row, "class") == character.name + "2") break;
        if (row == inventory.rows().size())
            throw std::runtime_error("Missing 800px player inventory layout: " + character.name);
        const auto number = [&](const std::string &field) {
            const auto value = inventory.number(row, field);
            if (!value) throw std::runtime_error("Missing player inventory field: " + field);
            return *value;
        };
        const int left = number("invLeft"), top = number("invTop");
        PlayerInventoryLayout layout;
        if (!originalRules) {
            layout.top = top;
            layout.height = number("invBottom") - top;
            if (number("invRight") - left != 320 || top < 0 || layout.height < 1 || top + layout.height > 558)
                throw std::runtime_error("Unsupported remote player inventory panel: " + character.name);
        }
        layout.grid = {number("gridX"), number("gridY"), number("gridLeft") - left,
            number("gridTop") - top, number("gridBoxWidth"), true};
        const auto &grid = layout.grid;
        if (grid.columns < 1 || grid.columns > 16 || grid.rows < 1 || grid.rows > 16 ||
            grid.cellSize < 1 || grid.cellSize != number("gridBoxHeight") || grid.left < 0 || grid.top < 0 ||
            grid.left + grid.columns * grid.cellSize > 320 || grid.top + grid.rows * grid.cellSize > layout.height)
            throw std::runtime_error("Unsupported player inventory grid: " + character.name);
        constexpr const char *slots[]{"head", "neck", "torso", "rArm", "lArm",
            "rHand", "lHand", "belt", "feet", "gloves", "rArm", "lArm"};
        static_assert(std::size(slots) == size_t(EquipmentSlot::Count));
        for (size_t slot = 0; slot < std::size(slots); ++slot) {
            const std::string prefix = slots[slot];
            auto &box = layout.slots[slot];
            box = {number(prefix + "Left") - left, number(prefix + "Top") - top,
                number(prefix + "Right") - left, number(prefix + "Bottom") - top};
            if (box[0] < 0 || box[1] < 0 || box[2] > 320 || box[3] > layout.height ||
                box[2] <= box[0] || box[3] <= box[1] ||
                box[2] - box[0] != number(prefix + "Width") || box[3] - box[1] != number(prefix + "Height"))
                throw std::runtime_error("Unsupported player inventory slot: " + character.name + "/" + prefix);
        }
        data.playerInventoryLayouts.emplace(character.code, std::move(layout));
    }
    data.hirelings = loadHirelingDefinitions(data.tables.at("hireling"), data.tables.at("skills"));
    {
        data.tables.emplace("skilldesc", DataTable(archives.read("data/global/excel/skilldesc.txt")));
        data.staffRecipe = loadStaffRecipeContent(data.items, data.tables);
        data.goldenBird = loadGoldenBirdContent(data.items, data.tables);
        data.gidbinnCode = loadNativeQuestItem(data.items, data.tables, "g33", 18);
        data.lamTomeCode = loadNativeQuestItem(data.items, data.tables, "bbb", 16);
        data.soulstoneCode = loadNativeQuestItem(data.items, data.tables, "mss", 23);
        data.hellforge = loadHellforgeContent(data.items, data.tables);
        data.prisonOfIce = loadPrisonOfIceContent(data.items, data.tables);
        data.khalimRecipe = loadKhalimRecipeContent(data.items, data.tables);
        data.hirelingLayout = loadHirelingLayout(data.tables.at("inventory"));
        data.itemStrings = strings.entries();
        // The native tooltip uses numeric TBL IDs; the ethereal entry shares key "X".
        data.itemStrings["itemEthereal"] = strings.find(22745);
        data.itemStrings["itemEnhancedDamage"] = strings.find(10023);
        const DataTable hireDescriptions(archives.read("data/global/excel/hiredesc.txt"));
        for (size_t row = 0; row < hireDescriptions.rows().size(); ++row) {
            const auto code = hireDescriptions.value(row, "Code");
            const auto label = hireDescriptions.value(row, "Hireling Description");
            if (!code.empty()) data.hirelingDescriptions.emplace(code, label);
        }
        for (const auto &[key, value] : strings.entries())
            if ((key.size() >= 7 && key.starts_with("qstsa") && key[5] >= '1' && key[5] <= '5' && key[6] == 'q') || key.starts_with("qsta5q1") || key == "newquestlog" ||
                key.starts_with("qsts") || key == "noactivequest")
                data.questStrings.emplace(key, value);
            else if (std::any_of(data.hirelings.begin(),data.hirelings.end(),[&](const auto &definition) {
                const auto prefix=definition.nameFirst.find_last_not_of("0123456789")+1;
                return prefix<definition.nameFirst.size() && key.size()==definition.nameFirst.size() &&
                    definition.nameLast.size()==key.size() && key.starts_with(definition.nameFirst.substr(0,prefix)) &&
                    key>=definition.nameFirst && key<=definition.nameLast &&
                    std::all_of(key.begin()+prefix,key.end(),[](char c){return c>='0' && c<='9';});
            })) {
                data.hirelingStrings.emplace(key, value);
                data.hirelingNameIds.emplace(key, strings.index(key));
            }
        data.skills = loadSkillCatalog(data.tables.at("skills"), data.tables.at("skilldesc"),
                                       data.tables.at("charstats"), data.characters, strings, preparation);
        data.skills.descriptions = std::make_shared<const SkillDescriptionCatalog>(loadSkillDescriptions(data.tables, strings,
            archives.read("data/global/excel/missiles.bin")));
        if (originalRules) loadSkillAnimations(data.skills, data.tables.at("skills"), data.tables.at("weapons"), data.characters, archives);
        data.playerDeath = loadPlayerDeathData(archives, data.characters, data.tables.at("difficultylevels"));
        data.shrines = loadShrines(data.tables.at("shrines"));
        const auto &stateTable=data.tables.emplace("states",DataTable(archives.read("data/global/excel/states.txt"))).first->second;
        data.states = loadCombatStates(stateTable);
        auto nativeStates=std::make_shared<CombatStateRules>();
        for(const auto &[name,state]:data.states) nativeStates->emplace(state.definition.id,state.definition);
        data.nativeStates=std::move(nativeStates);
        data.tables.emplace("overlay", DataTable(archives.read("data/global/excel/overlay.txt")));
        if (originalRules) {
            loadAuraSkills(data);
            loadMonsterEnchantmentResources(data, archives);
            const auto &overlays = data.tables.at("overlay");
            const DataTable sounds(archives.read("data/global/excel/sounds.txt"));
            loadSorceressEffects(data.skills, data.tables.at("skills"), data.tables.at("missiles"),
                                 overlays, sounds, data.states, archives);
            loadNativeDefensiveBuffs(data.skills,data.tables.at("skills"),overlays,sounds,data.states,archives);
            loadWeaponSkills(data.skills, data.tables.at("skills"), data.tables.at("missiles"), sounds, archives);
            loadAmazonBowSkills(data.skills, data.tables.at("skills"), data.tables.at("missiles"), sounds, archives);
            loadAmazonSpearSkills(data.skills, data.tables.at("skills"), data.tables.at("missiles"), sounds, archives);
            loadAmazonMagicSkills(data.skills, data.tables.at("skills"), overlays, sounds, data.states, archives);
            loadAmazonSummons(data, overlays, sounds, archives);
            loadPaladinSkills(data.skills, data.tables.at("skills"), data.tables.at("missiles"), overlays, sounds, data.states, archives);
            loadBarbarianSkills(data, archives);
            loadNecromancerCurses(data.skills, data.tables.at("skills"), overlays, sounds, data.states, archives);
            loadBoneSkills(data.skills, data.tables.at("skills"), data.tables.at("missiles"), overlays, sounds, data.states, archives);
            loadNecromancerSummons(data.skills, data.tables.at("skills"), data.tables.at("monstats"),
                data.tables.at("monstats2"), data.tables.at("monlvl"), sounds, archives);
            loadRemainingNecromancerSummons(data.skills, data.tables.at("skills"), data.tables.at("monstats"),
                data.tables.at("monstats2"), data.tables.at("monlvl"), sounds, data.tables.at("missiles"), data.states, archives, data);
        }
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
    data.petPortraits = loadPetPortraitData(data.tables.at("pettype"), strings);
    data.armorTypes = std::move(armorTypes);
    data.vendors = loadVendorData(data.tables, data.items);
    if (archives.contains("data/local/docs/eng/a1npc.txt")) {
        const Language english;
        const ClassicStrings englishStrings(archives, english);
        data.npcDialogues = loadNpcDialogues(archives, data.tables.at("monstats"),
            DataTable(archives.read("data/global/excel/monpreset.txt")), data.itemStrings, englishStrings);
    }
    data.questContent = loadQuestContent(archives, data.npcDialogues, data.questStrings);
    {
        loadPropertyData(data);
        if (originalRules) {
            loadSocketData(data, strings);
            loadCubeData(data);
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
            prepareEquipmentSetData(data);
            loadMagicAffixData(data);
            loadLodTreasureData(data);
        } else loadRemoteItemDisplayData(data, strings);
        data.nativeStats=prepareNativeStatRules(data);
        if (originalRules) loadItemConsumables(data);
        if (originalRules) data.nativeFormulas=prepareSkillFormulaCatalog(data);
        if (originalRules) loadAssassinSkills(data, archives);
        if (originalRules) loadDruidSkills(data, archives);
        data.nativeMonsterTypes=loadMonsterTypeCatalog(data.tables.at("monstats"),data.tables.at("montype"),data.tables.at("monstats2"));
        if (originalRules) {
            const auto ratios=loadMonsterModeLevelRatios(data.tables.at("monlvl"));
            const auto lifeRatios=loadMonsterLifeLevelRatios(data.tables.at("monlvl"));
            const auto &monsters=data.tables.at("monstats");
            for(size_t row=0;row<monsters.rows().size();++row) if(const auto id=monsters.number(row,"hcIdx"))
                for(int difficulty=0;difficulty<3;++difficulty) {
                    if(!data.nativeMonsterModes.emplace(std::pair{*id,difficulty},loadMonsterModeTable(monsters,row,difficulty,ratios)).second)
                        throw std::runtime_error("Duplicate native monster mode table identity");
                    if(!data.nativeMonsterLives.emplace(std::pair{*id,difficulty},loadMonsterLifeTable(monsters,row,difficulty,lifeRatios)).second)
                        throw std::runtime_error("Duplicate native monster life table identity");
                }
        }
        return data;
    }
}
} // namespace d2x

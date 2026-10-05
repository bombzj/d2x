#include "content/classic_data.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_loot.hpp"
#include "content/items/item_quality.hpp"
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include "resources/text.hpp"
#include "persistence/save_file.hpp"
#include "gameplay/items/equipment_rules.hpp"
#include "world/cow_level.hpp"
#include "world/maze.hpp"
#include "world/outdoor/outdoor.hpp"
#include "world/population.hpp"
#include "world/world_report.hpp"
#include "world/region.hpp"
#include "world/outdoor/native_act_layout.hpp"
#include "world/retail/outdoor_layout.hpp"
#include "world/retail/room_data.hpp"
#include "world/retail/preset_room.hpp"
#include "world/retail/tile_materialization.hpp"
#include "world/retail/preset_scan.hpp"
#include "world/native_map.hpp"
#include <nlohmann/json.hpp>
#include "core/random.hpp"
#include "core/random_seed.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <raylib.h>

int main(int argc, char **argv) {
    try {
        if (argc < 3) {
            std::cout
                << "d2x_assets <archive-or-folder> list [wildcard]\n  ... text <txt-member>\n  ... hex <member> [byte-count]\n  ... ds1-paths <ds1-member>\n  ... extract <member> <destination>\n  "
                   "... preview <dc6-or-dcc-member> <sheet.png> [first-frame] [columns] [frame-count]\n  ... pack <manifest.txt> <new.mpq>\n"
                   "  ... item <code>\n  ... drops <monster-class>\n  ... maps [level-ID] [map-seed]\n"
                   "  ... presets [name-filter]\n  ... maze <level-ID> [map-seed] [difficulty:0-2]\n"
                   "  ... outdoor <level-ID> [map-seed]\n"
                   "  ... native-layout <act:1-5> <map-seed> [difficulty:0-2]\n"
                   "  ... native-outdoor <level> <map-seed> [difficulty:0-2]\n"
                   "  ... native-room <level> <map-seed> <tile-x> <tile-y> [difficulty:0-2]\n"
                   "  ... native-map <level> <map-seed> <difficulty:0-2> [original-export.json|complete]\n"
                   "  ... substitutions <LvlSub-type>\n"
                   "  ... save-info <file.d2s>\n"
                   "  ... treasure <TC-name> [seed] [monster-level]\n"
                   "  ... quality <item-code> <item-level> <MF> [seed] [unique set rare magic modifiers]\n"
                   "  ... loot-plan <TC-name> <item-level> <seed> [upgrade-level]\n"
                   "  ... special <unique|set> <item-code> <item-level> <seed>\n"
                   "  ... loot-entry <monster> <normal|champion|unique|minion|boss|superunique> <difficulty:0-2> <level-ID> [superunique-ID]\n"
                   "  ... population <level-ID> [normal|nightmare|hell] [seed]\n";
            return 0;
        }
        d2x::Archives a;
        a.mountDirectory(argv[1]);
        std::string command = argv[2];
        if (command == "loot-plan" && (argc == 6 || argc == 7)) {
            auto data = d2x::loadClassicData(a);
            auto ratios = data.tables.find("itemratio");
            if (ratios == data.tables.end())
                throw std::runtime_error("Missing original ItemRatio table");
            auto integer = [](std::string_view text) {
                int value = 0;
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (error != std::errc{} || end != text.data() + text.size() || value < 0 || value > 99)
                    throw std::runtime_error("Invalid loot plan level");
                return value;
            };
            auto text = std::string_view(argv[5]);
            uint64_t seed = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
            if (error != std::errc{} || end != text.data() + text.size())
                throw std::runtime_error("Invalid loot plan seed");
            auto plan = d2x::planItemLoot(data, ratios->second, argv[3], integer(argv[4]),
                                               argc == 7 ? integer(argv[6]) : 0, seed);
            std::cout << "TC=" << argv[3] << " seed=" << seed << " next=" << plan.randomState
                      << " NoDrop=" << plan.noDrops << " drops=" << plan.drops.size()
                      << " deferred=" << plan.deferred << '\n';
            for (const auto &drop : plan.drops)
                std::cout << "  " << drop.code << " quantity=" << drop.quantity << " level="
                          << drop.level << " quality=" << int(drop.generation.quality)
                          << " specialRow=" << drop.generation.specialRow
                          << " gradeRow=" << drop.generation.gradeRow
                          << " affixes=" << drop.generation.affixes.size()
                          << " offset=" << drop.offset.x << ',' << drop.offset.y << '\n';
            std::cout << "Shared item planner; no session state or items were created.\n";
        } else if (command == "special" && argc == 7) {
            auto data = d2x::loadClassicData(a);
            auto kind = std::string_view(argv[3]);
            if (kind != "unique" && kind != "set")
                throw std::runtime_error("Special item kind must be unique or set");
            int level = 0;
            auto levelText = std::string_view(argv[5]);
            auto [levelEnd, levelError] = std::from_chars(levelText.data(), levelText.data() + levelText.size(), level);
            if (levelError != std::errc{} || levelEnd != levelText.data() + levelText.size())
                throw std::runtime_error("Invalid special item level");
            uint64_t seed = 0;
            auto seedText = std::string_view(argv[6]);
            auto [seedEnd, seedError] = std::from_chars(seedText.data(), seedText.data() + seedText.size(), seed);
            if (seedError != std::errc{} || seedEnd != seedText.data() + seedText.size())
                throw std::runtime_error("Invalid special item seed");
            const auto &records = kind == "unique" ? data.uniqueItems : data.setItems;
            auto roll = d2x::rollSpecialItem(records, argv[4], level, seed);
            std::cout << "Requested=" << kind << " base=" << argv[4] << " level=" << level
                      << " seed=" << seed << " next=" << roll.randomState << '\n';
            if (roll.row)
                for (const auto &record : records)
                    if (record.row == *roll.row) {
                        std::cout << "  row=" << record.row << " name=" << record.name
                                  << " set=" << record.set << " rarity=" << record.rarity << '\n';
                        break;
                    }
            if (!roll.row)
                std::cout << "  no eligible original record\n";
            std::cout << "Selection only; properties and an item instance were not created.\n";
        } else if (command == "quality" && (argc == 6 || argc == 7 || argc == 11)) {
            auto data = d2x::loadClassicData(a);
            d2x::DataTable ratios(a.read("data/global/excel/itemratio.txt"));
            auto rules = d2x::loadItemQualityRules(data, ratios, argv[3]);
            auto integer = [](std::string_view text) {
                int value = 0;
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (error != std::errc{} || end != text.data() + text.size())
                    throw std::runtime_error("Invalid quality argument");
                return value;
            };
            uint64_t seed = d2x::initialRandom(d2x::freshSeed());
            if (argc >= 7) {
                auto text = std::string_view(argv[6]);
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
                if (error != std::errc{} || end != text.data() + text.size())
                    throw std::runtime_error("Invalid quality seed");
            }
            std::array<int, 4> modifiers{};
            if (argc == 11)
                for (size_t index = 0; index < modifiers.size(); ++index)
                    modifiers[index] = integer(argv[7 + index]);
            auto result = d2x::rollItemQuality(rules, integer(argv[4]), integer(argv[5]), modifiers, seed);
            std::cout << "Item=" << argv[3] << " baseLevel=" << rules.baseLevel
                      << " requestedQuality=" << d2x::dropQualityName(result.quality)
                      << " seed=" << seed << " next=" << result.randomState << '\n';
            for (const auto &check : result.checks)
                std::cout << "  " << d2x::dropQualityName(check.quality) << " chance=" << check.chance
                          << " roll=" << check.roll << " threshold=128\n";
            std::cout << "Quality request only; unique/set availability, affixes and instance generation "
                         "are not executed.\n";
        } else if (command == "loot-entry" && (argc == 7 || argc == 8)) {
            auto data = d2x::loadClassicData(a);
            d2x::MonsterCatalog monsters(a, data.tables.at("monstats"));
            d2x::WorldCatalog world(a);
            d2x::LootRequest request;
            request.identity.monster = argv[3];
            const std::pair<std::string_view, d2x::MonsterRank> ranks[] = {
                {"normal", d2x::MonsterRank::Normal}, {"champion", d2x::MonsterRank::Champion},
                {"unique", d2x::MonsterRank::Unique}, {"minion", d2x::MonsterRank::Minion},
                {"boss", d2x::MonsterRank::Boss}, {"superunique", d2x::MonsterRank::SuperUnique}};
            auto rank = std::find_if(std::begin(ranks), std::end(ranks),
                                      [&](const auto &entry) { return entry.first == argv[4]; });
            if (rank == std::end(ranks))
                throw std::runtime_error("Unknown loot rank");
            request.identity.rank = rank->second;
            if (argc == 8)
                request.identity.superUnique = argv[7];
            if ((argc == 8) != (request.identity.rank == d2x::MonsterRank::SuperUnique))
                throw std::runtime_error("Super unique rank requires exactly one super unique ID");
            auto integer = [](std::string_view text) {
                int value = 0;
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (error != std::errc{} || end != text.data() + text.size())
                    throw std::runtime_error("Invalid loot entry integer");
                return value;
            };
            request.difficulty = integer(argv[5]);
            request.region = d2x::RegionId(integer(argv[6]));
            auto entry = d2x::resolveMonsterLoot(data, monsters, world, request);
            std::cout << "Monster=" << request.identity.monster << " rank=" << argv[4]
                      << " itemLevel=" << entry.itemLevel << " upgradeLevel=" << entry.upgradeLevel
                      << " TC=" << entry.treasureClass << '\n';
            std::cout << (entry.status == d2x::LootEntryStatus::Ready ? "Ready"
                         : entry.status == d2x::LootEntryStatus::Empty ? "Empty" : "Deferred")
                      << ": " << entry.reason << '\n';
        } else if (command == "save-info" && argc == 4) {
            auto data = d2x::loadClassicData(a);
            auto snapshot = d2x::loadSave(argv[3], data);
            std::cout << "Save format=D2S-v96 name=" << snapshot.player.name
                      << " lastRegion=" << int(snapshot.lastRegion)
                      << " gold=" << snapshot.player.gold
                      << " level=" << snapshot.player.level
                      << " xp=" << snapshot.player.experience
                      << " unspentAttributes=" << snapshot.player.unspentAttributes
                      << " allocated=" << snapshot.player.allocated.strength << ','
                      << snapshot.player.allocated.dexterity << ','
                      << snapshot.player.allocated.vitality << ','
                      << snapshot.player.allocated.energy
                      << " life=" << snapshot.player.hp
                      << " creationRandom=" << snapshot.inventory.creationRandom << '\n';
            for (const auto &[id, item] : snapshot.inventory.items) {
                auto location = std::get_if<d2x::ContainerLocation>(&item.location);
                if (!location || (location->container != snapshot.containers.equipment &&
                                  location->container != snapshot.containers.beltEquipment))
                    continue;
                const auto *definition = data.items.find(item.definition);
                auto slot = location->container == snapshot.containers.beltEquipment
                                ? d2x::EquipmentSlot::Belt : d2x::EquipmentSlot(location->cell.x);
                std::cout << "  " << d2x::equipmentSlotCode(slot) << " " << item.definition
                          << " id=" << id.value << " revision=" << item.revision
                          << " durability=" << item.durability << '/'
                          << (definition ? definition->maxDurability : 0)
                          << " defense=" << item.defense << '\n';
            }
            std::cout << "Decoded character save only; full gameplay validation occurs on --load.\n";
        } else if (command == "substitutions" && argc == 4) {
            d2x::WorldCatalog catalog(a);
            int type = std::stoi(argv[3]);
            for (const auto &record : catalog.substitutions()) {
                if (record.type != type)
                    continue;
                d2x::MapData data;
                try {
                    data = d2x::decodeDs1(a.read(record.file), record.file);
                } catch (const std::exception &error) {
                    throw std::runtime_error(record.file + ": " + error.what());
                }
                std::cout << record.name << " " << record.file << " version=" << data.version
                          << " method=" << data.substitutionMethod
                          << " groups=" << data.substitutionGroups.size()
                          << " declared=" << data.declaredSubstitutionGroups
                          << " zeroFilled=" << data.zeroFilledSubstitutionGroups << '\n';
                for (const auto &group : data.substitutionGroups) {
                    std::cout << "  group " << group.x << ',' << group.y << " size=" << group.width << 'x'
                              << group.height << " variants=" << group.variants << '\n';
                    for (int variant = 0; variant <= group.variants; ++variant) {
                        std::cout << "    " << (variant ? "replace" : "match") << ' ' << variant << '\n';
                        for (int row = 0; row < group.height; ++row) {
                            for (int column = 0; column < group.width; ++column) {
                                int x = group.x + variant * (group.width + 1) + column;
                                int y = group.y + row;
                                if (x >= data.width)
                                    throw std::runtime_error("Substitution variant exceeds DS1 bounds");
                                auto index = size_t(y) * data.width + x;
                                auto wall = data.walls.empty() ? 0u : data.walls.front()[index].value;
                                auto floor = data.floors.empty() ? 0u : data.floors.front()[index].value;
                                if (wall & 1)
                                    std::cout << ' ' << int((wall >> 8) & 255) - 1;
                                else
                                    std::cout << ((floor & 2) ? " ." : " _");
                            }
                            std::cout << '\n';
                        }
                    }
                }
            }
        } else if ((command == "maze" || command == "outdoor") && argc >= 4 && argc <= 6) {
            d2x::WorldCatalog catalog(a);
            uint32_t seed = d2x::freshSeed();
            if (argc >= 5) {
                std::string value = argv[4];
                auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seed);
                if (error != std::errc{} || end != value.data() + value.size())
                    throw std::runtime_error("Map seed expects uint32");
            }
            const int level = std::stoi(argv[3]);
            d2x::MapRecipe recipe;
            if (command == "maze")
                recipe = d2x::generateMaze(a, catalog, level, seed, argc == 6 ? std::stoi(argv[5]) : 0);
            else {
                d2x::WorldSelection selection;
                selection.level = level;
                selection.seed = seed;
                const auto plan = d2x::planWorld(a, catalog, selection);
                const auto region = std::find_if(plan.regions.begin(), plan.regions.end(),
                    [&](const auto &entry) { return int(entry.definition.id) == level; });
                if (region == plan.regions.end()) throw std::runtime_error("Level has no runtime terrain");
                recipe = region->recipe;
            }
            std::cout << recipe.ds1 << " rooms=" << recipe.pieces.size() << '\n';
            for (const auto &room : recipe.pieces)
                std::cout << "  room " << room.x << ',' << room.y << " size=" << room.width << 'x'
                          << room.height << " preset=" << room.preset << " variant=" << room.variant << " "
                          << room.ds1 << '\n';
            d2x::TileLibraryCache cache(a);
            d2x::Map map;
            map.load(a, cache, recipe, seed);
            for (const auto &b : recipe.boundaries) {
                std::cout << "  boundary -> " << b.destination << " side=" << b.side << " span=" << b.start
                          << ':' << b.end << '\n';
                for (int depth : {3, 8, 15, 25, 35}) {
                    std::cout << "    depth " << depth << ' ';
                    for (int t = b.start * 5; t < b.end * 5; ++t) {
                        int x = b.side == 1 ? depth : b.side == 3 ? recipe.width * 5 - depth : t;
                        int y = b.side == 2 ? depth : b.side == 0 ? recipe.height * 5 - depth : t;
                        std::cout << (map.grid.walkable(x, y) ? '.' : '#');
                    }
                    std::cout << '\n';
                }
            }
            for (const auto &layer : map.terrain.data.walls)
                for (int y = 0; y < map.terrain.data.height; ++y)
                    for (int x = 0; x < map.terrain.data.width; ++x) {
                        const auto &cell = layer[y * map.terrain.data.width + x];
                        if (cell.occupied() && (cell.orientation == 10 || cell.orientation == 11))
                            std::cout
                                << "  warp marker " << x << ',' << y << " style=" << ((cell.value >> 20) & 63)
                                << " sequence=" << ((cell.value >> 8) & 255) << " type=" << cell.orientation
                                << " hidden=" << cell.hidden() << " tile=" << map.tileIndex(cell, x, y)
                                << '\n';
                    }
        } else if (command == "population" && argc >= 4 && argc <= 6) {
            d2x::PopulationSettings settings;
            settings.seed = d2x::freshSeed();
            if (argc >= 5) {
                std::string difficulty = argv[4];
                if (difficulty == "normal")
                    settings.difficulty = 0;
                else if (difficulty == "nightmare")
                    settings.difficulty = 1;
                else if (difficulty == "hell")
                    settings.difficulty = 2;
                else
                    throw std::runtime_error("Difficulty expects normal, nightmare or hell");
            }
            if (argc == 6) {
                std::string seed = argv[5];
                auto [end, error] = std::from_chars(seed.data(), seed.data() + seed.size(), settings.seed);
                if (error != std::errc{} || end != seed.data() + seed.size())
                    throw std::runtime_error("Seed expects an unsigned 32-bit decimal integer");
            }
            d2x::WorldCatalog world(a);
            d2x::DataTable stats(a.read("data/global/excel/monstats.txt"));
            d2x::MonsterCatalog monsters(a, stats);
            const auto &level = world.level(std::stoi(argv[3]));
            auto available = world.availability(a, level.id);
            if (level.act != 0 || !available.ready())
                throw std::runtime_error("Population inspection requires an available Act I preset level");
            const auto &recipe = *available.recipe;
            const auto &preset = world.presets().at(recipe.preset);
            d2x::Map map;
            d2x::TileLibraryCache cache(a);
            map.load(a, cache, recipe, settings.seed);
            auto plan = d2x::planPopulation(monsters, &level, preset, map, settings);
            d2x::writePopulationReport(std::cout, plan, &level, preset, settings);
        } else if (command == "native-map" && (argc == 6 || argc == 7)) {
            using Json = nlohmann::json;
            auto integer = [](std::string_view text, auto &value) {
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (error != std::errc{} || end != text.data() + text.size())
                    throw std::runtime_error("Invalid native-map argument");
            };
            int level{}, difficulty{}; uint32_t seed{};
            integer(argv[3], level); integer(argv[4], seed); integer(argv[5], difficulty);
            d2x::WorldCatalog catalog(a, difficulty);
            d2x::TileLibraryCache cache(a);
            d2x::NativeMapGenerator generator(a, catalog, cache, catalog.level(level).act, seed, difficulty);
            const auto allocated = generator.levelRooms(level);
            const auto &placement = generator.layout().levels.at(level);
            Json report{{"id", level}, {"seed", seed}, {"difficulty", difficulty},
                {"offset", {{"x", placement.x * 5}, {"y", placement.y * 5}}},
                {"size", {{"width", placement.width * 5}, {"height", placement.height * 5}}},
                {"rooms", Json::array()}, {"samples", Json::array()}, {"events", Json::array()}};
            for (auto room = allocated.rbegin(); room != allocated.rend(); ++room) {
                const auto &r = generator.tiles().rooms().at(*room).room;
                report["rooms"].push_back({{"x", r.x}, {"y", r.y}, {"width", r.width},
                    {"height", r.height}, {"preset", r.preset}, {"file", r.file}, {"flags", r.flags},
                    {"initialSeed", r.seed.initial}, {"outdoorFlags", r.outdoorFlags},
                    {"auxiliary", r.auxiliary}, {"themeMask", r.themeMask}});
            }
            if (argc == 7 && std::string_view(argv[6]) == "complete") {
                const auto recipe = generator.recipe(level);
                const auto snapshot = generator.completeLevel(level);
                report["offset"] = {{"x", snapshot.tileX * 5}, {"y", snapshot.tileY * 5}};
                report["size"] = {{"width", snapshot.map.grid.width}, {"height", snapshot.map.grid.height}};
                report["collision"] = snapshot.collision;
                report["boundaries"] = Json::array();
                for (const auto &b : recipe.boundaries)
                    report["boundaries"].push_back({{"destination", b.destination}, {"side", b.side},
                        {"plane", b.plane}, {"start", b.start}, {"end", b.end}});
                report["objects"] = Json::array();
                for (const auto &o : snapshot.map.terrain.data.objects)
                    report["objects"].push_back({{"type", o.type}, {"id", o.id}, {"x", o.x}, {"y", o.y},
                        {"nativeIdentity", o.nativeIdentity}});
                report["exits"] = Json::array();
                for (const auto &exit : snapshot.map.terrain.exits)
                    report["exits"].push_back({{"slot", exit.slot}, {"id", exit.selection.id},
                        {"destination", exit.destination}, {"x", exit.position.x}, {"y", exit.position.y}});
                std::cout << report.dump() << '\n';
                return 0;
            }
            Json events = Json::array();
            if (argc == 7) {
                std::ifstream input(argv[6]);
                if (!input) throw std::runtime_error("Original map export cannot be opened");
                const auto original = Json::parse(input);
                if (original.at("id").get<int>() != level)
                    throw std::runtime_error("Original export belongs to another level");
                if (original.at("seed").get<uint32_t>() != seed || original.at("difficulty").get<int>() != difficulty)
                    throw std::runtime_error("Original export has a different seed or difficulty");
                events = original.at("native").at("events");
                if (!events.is_array() || events.size() > 65536)
                    throw std::runtime_error("Invalid original activation sequence");
            } else for (const auto &room : report["rooms"])
                for (const auto *op : {"reveal", "sample", "hide"})
                    events.push_back({{"op", op}, {"level", level}, {"x", room.at("x")}, {"y", room.at("y")}});
            try {
            for (const auto &event : events) {
                const auto op = event.at("op").get<std::string>();
                const int area = event.at("level").get<int>(), x = event.at("x").get<int>(), y = event.at("y").get<int>();
                if (catalog.level(area).act != catalog.level(level).act || x < 0 || y < 0 || x > 13107 || y > 13107)
                    throw std::runtime_error("Invalid original room coordinates");
                report["events"].push_back(event);
                if (op == "reveal") generator.reveal(area, x, y);
                else if (op == "hide") generator.hide(area, x, y);
                else if (op == "sample") {
                    // Native sight propagation can activate a room without a
                    // direct AddRoomData call. Sample that existing room only;
                    // do not synthesize an extra reveal and change the order.
                    const d2x::RetailRoomCollision *grid = nullptr;
                    for (size_t index = 0; index < generator.tiles().rooms().size(); ++index) {
                        const auto &room = generator.tiles().rooms()[index];
                        if (room.level == area && room.room.x == x && room.room.y == y) {
                            grid = generator.tiles().collision(index);
                            break;
                        }
                    }
                    if (!grid) throw std::runtime_error("Original activation sequence has no matching active collision");
                    report["samples"].push_back({{"level", area}, {"x", grid->x}, {"y", grid->y},
                        {"width", grid->width}, {"height", grid->height}, {"flags", grid->flags}});
                    auto &sample = report["samples"].back();
                    sample["near"] = Json::array();
                    for (size_t index = 0; index < generator.tiles().rooms().size(); ++index) {
                        const auto &room = generator.tiles().rooms()[index];
                        if (room.level != area || room.room.x != x || room.room.y != y) continue;
                        for (auto other : room.activeNear) {
                            if (!generator.tiles().collision(other)) continue;
                            const auto &active = generator.tiles().rooms()[other];
                            Json data{{"level", active.level}, {"x", active.room.x}, {"y", active.room.y},
                                {"tiles", Json::array()}, {"units", Json::array()}, {"pops", Json::array()}};
                            for (const auto &popup : active.roofPopups)
                                data["pops"].push_back({{"x", popup.x}, {"y", popup.y},
                                    {"width", popup.width}, {"height", popup.height}, {"group", popup.group},
                                    {"main", popup.roofMain}, {"pad", popup.pad}});
                            for (const auto &unit : active.units)
                                data["units"].push_back({{"type", unit.type}, {"id", unit.id},
                                    {"x", unit.x}, {"y", unit.y}});
                            for (const auto &unit : active.authoredUnits)
                                data["units"].push_back({{"type", unit.unit.type}, {"id", unit.unit.id},
                                    {"x", unit.unit.x}, {"y", unit.unit.y}});
                            auto append = [&](const auto &indices, const char *array) {
                                for (auto tileIndex : indices) {
                                    const auto &tile = generator.tiles().tiles()[tileIndex];
                                    const auto identity = active.libraries->identity(*tile.tile);
                                    data["tiles"].push_back({{"array", array}, {"x", tile.x}, {"y", tile.y},
                                        {"library", identity.first}, {"record", identity.second},
                                        {"type", tile.type}, {"flags", tile.flags}, {"orientation", tile.tile->orientation},
                                        {"main", tile.tile->main}, {"sub", tile.tile->sub}, {"rarity", tile.tile->rarity},
                                        {"collision", tile.tile->flags}});
                                }
                            };
                            append(active.floors, "floor"); append(active.walls, "wall"); append(active.shadows, "shadow");
                            sample["near"].push_back(std::move(data));
                        }
                    }
                } else throw std::runtime_error("Unknown original room operation");
            }
            } catch (const std::exception &e) {
                report["error"] = e.what();
                report["failedEvent"] = report["events"].size() - 1;
                std::cout << report.dump() << '\n';
                return 1;
            }
            std::cout << report.dump() << '\n';
        } else if (((command == "native-layout" || command == "native-outdoor") && (argc == 5 || argc == 6)) ||
                   (command == "native-room" && (argc == 7 || argc == 8))) {
            int act{}, difficulty{};
            uint32_t seed{};
            auto parse = [](std::string_view text, auto &value) {
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (error != std::errc{} || end != text.data() + text.size())
                    throw std::runtime_error("Invalid native-layout argument");
            };
            parse(argv[3], act); parse(argv[4], seed);
            if (command == "native-room") {
                if (argc == 8) parse(argv[7], difficulty);
            } else if (argc == 6) parse(argv[5], difficulty);
            if (difficulty < 0 || difficulty > 2 || (command == "native-layout" && (act < 1 || act > 5)))
                throw std::runtime_error("Invalid native map act or difficulty");
            d2x::WorldCatalog catalog(a, difficulty);
            if (command == "native-outdoor" || command == "native-room") {
                const auto layout = d2x::placeNativeAct(catalog, catalog.level(act).act, seed);
                const auto outdoor = d2x::buildRetailOutdoorLayout(a, catalog, layout, act);
                if (command == "native-room") {
                    int tileX{}, tileY{};
                    parse(argv[5], tileX); parse(argv[6], tileY);
                    const auto found = std::find_if(outdoor.rooms.begin(), outdoor.rooms.end(),
                        [&](const auto &room) { return room.x == tileX && room.y == tileY; });
                    if (found == outdoor.rooms.end()) throw std::runtime_error("No native room at this tile anchor");
                    std::map<std::string, d2x::MapData> patterns;
                    auto reader = [&](const std::string &path) -> const d2x::MapData & {
                        const auto key = d2x::normalize(path);
                        auto [entry, fresh] = patterns.try_emplace(key);
                        if (fresh) entry->second = d2x::decodeDs1(a.read(key), key);
                        return entry->second;
                    };
                    d2x::MapData grids;
                    d2x::TileLibraryCache cache(a);
                    d2x::RetailTileMaterializer tiles(catalog);
                    size_t roomIndex{};
                    const auto &warpSlots = layout.connections.at(act).warps;
                    if (found->preset) {
                        const auto &preset = catalog.presets().at(found->preset);
                        const auto &source = reader(preset.variants.at(size_t(found->file)));
                        auto room = d2x::buildRetailPresetRoomData(preset, *found, source);
                        auto libraries = std::make_shared<d2x::RetailTileSelector>(cache, catalog,
                            catalog.level(act).levelType, found->dt1Mask);
                        roomIndex = tiles.registerRoom(act, *found, std::move(libraries), warpSlots);
                        const std::array<size_t, 1> near{roomIndex};
                        d2x::RetailPresetScan scanner(a);
                        const auto saved = outdoor.presetUnits.find({act, found->preset, found->file,
                            found->mapX, found->mapY});
                        const std::span<const d2x::RetailPresetUnit> preloaded =
                            saved == outdoor.presetUnits.end() ? std::span<const d2x::RetailPresetUnit>{}
                            : std::span<const d2x::RetailPresetUnit>{saved->second};
                        tiles.loadPreset(roomIndex, room, near, [&](d2x::Seed &random) {
                            return scanner.buildUnits(source, catalog.level(act).act,
                                found->mapX, found->mapY, random);
                        }, preloaded);
                        grids = std::move(room.grids);
                        std::cout << "preset=" << found->preset << " file=" << found->file
                                  << " killEdge=" << room.killEdgeX << ',' << room.killEdgeY << '\n';
                    } else {
                        d2x::RetailPresetScan identities(a);
                        auto room = d2x::buildRetailOutdoorRoomData(cache, catalog, catalog.level(act),
                                                                  *found, outdoor.paths, reader, identities);
                        roomIndex = tiles.registerRoom(act, room.room, room.tiles, warpSlots);
                        const std::array<size_t, 1> near{roomIndex};
                        tiles.loadOutdoor(roomIndex, room, near);
                        grids.width = room.grids.width; grids.height = room.grids.height;
                        grids.floors.push_back(std::move(room.grids.floors));
                        grids.walls.push_back(std::move(room.grids.walls));
                        grids.shadows = std::move(room.grids.shadows);
                        std::cout << "themes=" << found->themeMask << " shadows=" << room.shadows.size()
                                  << " authoredUnits=" << room.grids.units.size() << '\n';
                        for (const auto &shadow : room.shadows)
                            std::cout << "shadow=" << shadow.x << ',' << shadow.y << " value=" << shadow.value
                                      << " key=" << shadow.tile->key() << " rarity=" << shadow.tile->rarity << '\n';
                    }
                    auto print = [&](const char *name, const std::vector<std::vector<d2x::MapCell>> &layers) {
                        for (size_t i = 0; i < layers.size(); ++i) {
                            std::cout << name << '=' << i << '\n';
                            for (int y = 0; y < grids.height; ++y) {
                                for (int x = 0; x < grids.width; ++x) {
                                    const auto &cell = layers[i].at(size_t(y) * size_t(grids.width) + size_t(x));
                                    std::cout << ' ' << cell.value << ':' << cell.orientation;
                                }
                                std::cout << '\n';
                            }
                        }
                    };
                    print("floor", grids.floors); print("wall", grids.walls);
                    const std::array<size_t, 1> active{roomIndex};
                    tiles.activateCollision(roomIndex, active);
                    const auto &collision = *tiles.collision(roomIndex);
                    const auto &materialized = tiles.rooms().at(roomIndex);
                    std::cout << "selectedFloors=" << materialized.floors.size()
                              << " selectedWalls=" << materialized.walls.size()
                              << " selectedShadows=" << materialized.shadows.size()
                              << " tileUnits=" << materialized.units.size()
                              << " collisionSubtiles=" << collision.width << ',' << collision.height << '\n';
                    for (const auto &tile : tiles.tiles())
                        std::cout << "tile=" << tile.x << ',' << tile.y << " type=" << tile.type
                                  << " flags=" << tile.flags << " key=" << tile.tile->key()
                                  << " rarity=" << tile.tile->rarity << '\n';
                    for (const auto &unit : materialized.units)
                        std::cout << "tileUnit=" << unit.type << ':' << unit.id << " position="
                                  << unit.x << ',' << unit.y << '\n';
                    for (const auto &entry : materialized.authoredUnits)
                        std::cout << "presetUnit=" << entry.unit.type << ':' << entry.unit.id
                                  << " resolved=" << entry.identityResolved << " mode=" << entry.mode
                                  << " position=" << entry.unit.x << ',' << entry.unit.y
                                  << " pathNodes=" << entry.unit.path.size() << '\n';
                    std::cout << "Isolated room only: no neighboring activation or established warp links; "
                                 "server terrain/collision equivalence not certified.\n";
                    return 0;
                }
                std::cout << "Layout and room allocation only; theme tiles and collision not certified. level=" << act
                          << " origin=" << outdoor.placement.x << ',' << outdoor.placement.y
                          << " grid=" << outdoor.grid.width() << ',' << outdoor.grid.height()
                          << " flags=" << outdoor.flags << '\n';
                for (int y = 0; y < outdoor.grid.height(); ++y)
                    for (int x = 0; x < outdoor.grid.width(); ++x) {
                        const auto &cell = outdoor.grid.cell(x, y);
                        std::cout << "cell=" << x << ',' << y << " preset=" << cell.preset
                                  << " file=" << ((cell.flags >> 16) & 15) << " flags=" << cell.flags
                                  << " vis=" << cell.links << " auxiliary=" << cell.auxiliary << '\n';
                    }
                for (size_t i = 0; i < outdoor.paths.size(); ++i) {
                    std::cout << "path=" << i;
                    for (const auto &point : outdoor.paths[i]) std::cout << ' ' << point.x << ',' << point.y;
                    std::cout << '\n';
                }
                for (const auto &room : outdoor.rooms)
                    std::cout << "room=" << room.x << ',' << room.y << " size=" << room.width << ','
                              << room.height << " preset=" << room.preset << " file=" << room.file
                              << " initialSeed=" << room.seed.initial << " flags=" << room.flags
                              << " dt1=" << room.dt1Mask << " themes=" << room.themeMask << '\n';
                return 0;
            }
            const auto layout = d2x::placeNativeAct(catalog, act - 1, seed);
            std::cout << "Placement only; terrain not certified. startSeed=" << layout.startSeed << '\n';
            if (layout.staffTomb)
                std::cout << "staffTomb=" << *layout.staffTomb << " bossTomb=" << *layout.bossTomb << '\n';
            for (const auto &[id, p] : layout.levels) {
                std::cout << "level=" << id << " origin=" << p.x << ',' << p.y << " size="
                          << p.width << ',' << p.height << " direction=" << p.direction
                          << " alignment=" << p.alignment << " outdoorFlags=" << p.outdoorFlags;
                if (p.presetVariant) std::cout << " variant=" << *p.presetVariant;
                std::cout << '\n';
            }
            for (const auto &[from, to] : layout.links)
                std::cout << "placement-link=" << from << ',' << to << '\n';
            for (const auto &[level, slots] : layout.connections)
                for (size_t i = 0; i < slots.visible.size(); ++i)
                    if (slots.visible[i]) std::cout << "vis=" << level << ':' << i << " destination="
                                                  << slots.visible[i] << " warp=" << slots.warps[i] << '\n';
        } else if (command == "maps" && (argc == 3 || argc == 4 || argc == 5)) {
            d2x::WorldCatalog catalog(a);
            if (argc == 5) {
                d2x::WorldSelection selection;
                selection.level = std::stoi(argv[3]);
                const std::string value = argv[4];
                auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), selection.seed);
                if (error != std::errc{} || end != value.data() + value.size())
                    throw std::runtime_error("Map seed expects uint32");
                const auto plan = d2x::planWorld(a, catalog, selection);
                d2x::DataTable stats(a.read("data/global/excel/monstats.txt"));
                d2x::MonsterCatalog monsters(a, stats);
                d2x::EntityIds ids;
                auto regions = d2x::loadRegions(a, ids, plan.regions, monsters, catalog,
                    selection.seed, selection.seed);
                d2x::linkLevelExits(regions, catalog);
                for (const auto &region : regions)
                    std::cout << "Connected region " << int(region.definition.id)
                        << " exits=" << region.exits.size() << " objects=" << region.objects.size() << '\n';
                return 0;
            }
            d2x::writeWorldReport(std::cout, a, catalog, argc == 4 ? std::stoi(argv[3]) : 0);
        } else if (command == "presets" && (argc == 3 || argc == 4)) {
            d2x::WorldCatalog catalog(a);
            std::string filter = argc == 4 ? d2x::normalize(argv[3]) : "";
            for (const auto &[id, preset] : catalog.presets()) {
                if (d2x::normalize(preset.name).find(filter) == std::string::npos)
                    continue;
                std::cout << id << " | " << preset.name << " | LevelId=" << preset.level
                          << " | Dt1Mask=" << preset.dt1Mask << '\n';
                for (int variant = 0; variant < 6; ++variant)
                    if (!preset.variants[variant].empty())
                        std::cout << "  " << variant << " " << preset.variants[variant] << " ["
                                  << (a.contains(preset.variants[variant]) ? "DS1 present" : "DS1 missing")
                                  << "]\n";
            }
        } else if (command == "treasure" && argc >= 4 && argc <= 6) {
            auto data = d2x::loadClassicData(a);
            if (data.profile != "lod-named-txt-v1")
                throw std::runtime_error("Treasure selection requires the LoD named table profile");
            uint64_t seed = d2x::initialRandom(d2x::freshSeed());
            if (argc >= 5) {
                auto text = std::string_view(argv[4]);
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
                if (error != std::errc{} || end != text.data() + text.size())
                    throw std::runtime_error("Invalid treasure seed");
            }
            int level = 0;
            if (argc == 6) {
                auto text = std::string_view(argv[5]);
                auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), level);
                if (error != std::errc{} || end != text.data() + text.size() || level < 0 || level > 99)
                    throw std::runtime_error("Invalid monster level (0-99)");
            }
            auto roll = d2x::selectTreasure(data.treasures, argv[3], seed, level);
            std::cout << "Single-player TC selection: " << argv[3] << " seed=" << seed
                      << " level=" << level << " next=" << roll.randomState
                      << " NoDrop=" << roll.noDrops << '\n';
            const auto &root = *std::find_if(data.treasures.begin(), data.treasures.end(),
                                             [&](const auto &record) { return record.name == roll.root; });
            std::cout << "Resolved root: " << root.name << " Picks=" << root.picks.value_or(1)
                      << " NoDropWeight=" << root.noDrop.value_or(0) << '\n';
            for (size_t index = 0; index < root.codes.size(); ++index)
                std::cout << "  " << root.codes[index] << " weight=" << root.weights[index] << '\n';
            for (const auto &selection : roll.selections) {
                for (const auto &step : selection.path)
                    std::cout << step << " -> ";
                std::cout << selection.code
                          << (data.items.find(selection.code) ? " [base item]" : " [unresolved token]")
                          << " quality(U/S/R/M)=";
                for (auto quality : selection.quality)
                    std::cout << quality << ' ';
                std::cout << '\n';
            }
            std::cout << "Selection only: unresolved tokens, item creation, quality and the six-item "
                         "creation limit are not executed.\n";
        } else if ((command == "item" || command == "drops") && argc == 4) {
            auto data = d2x::loadClassicData(a);
            std::cout << "Data profile: " << data.profile << '\n';
            if (command == "item") {
                auto item = data.items.find(argv[3]);
                if (!item)
                    throw std::runtime_error("Unknown item code");
                const auto &equipment = item->equipment;
                std::cout << "Equipment rules: "
                          << (equipment.known ? "original ItemTypes" : "unverified profile")
                          << " class=" << (equipment.requiredClass.empty() ? "any" : equipment.requiredClass)
                          << " twoHanded=" << equipment.twoHanded
                          << " oneOrTwoHanded=" << equipment.oneOrTwoHanded << " shoots=" << equipment.shoots
                          << " quiver=" << equipment.quiver << " slots=";
                for (size_t index = 0; index < equipment.slots.size(); ++index)
                    if (equipment.slots[index])
                        std::cout << d2x::equipmentSlotCode(d2x::EquipmentSlot(index)) << ' ';
                std::cout << " types=";
                for (const auto &type : equipment.types)
                    std::cout << type << ' ';
                std::cout << '\n';
                const auto &table = data.tables.at(item->base.sourceTable);
                const auto &row = table.rows().at(item->base.sourceRow);
                std::cout << item->base.sourceTable << ".txt row " << item->base.sourceRow << '\n';
                for (size_t column = 0; column < table.columns().size(); ++column)
                    if (!row[column].empty())
                        std::cout << column << " " << table.columns()[column] << " = " << row[column] << '\n';
            } else {
                auto monster = std::find_if(data.monsters.begin(), data.monsters.end(),
                                            [&](const auto &m) { return m.name == argv[3]; });
                if (monster == data.monsters.end())
                    throw std::runtime_error("Unknown monster class");
                const char *difficulties[] = {"Normal", "Nightmare", "Hell"};
                for (size_t difficulty = 0; difficulty < 3; ++difficulty) {
                    std::cout << difficulties[difficulty] << '\n';
                    for (size_t slot = 0; slot < 4; ++slot) {
                        auto index = monster->treasureClasses[difficulty][slot];
                        if (!index) {
                            std::cout << "  TC" << slot + 1 << " -> empty\n";
                            continue;
                        }
                        const auto &tc = data.treasures.at(*index);
                        std::cout << "  TC" << slot + 1 << " -> " << *index << " " << tc.name << ":";
                        if (tc.picks)
                            std::cout << " Picks=" << *tc.picks;
                        if (tc.noDrop)
                            std::cout << " NoDrop=" << *tc.noDrop;
                        for (size_t i = 0; i < tc.codes.size(); ++i) {
                            std::cout << ' ' << tc.codes[i];
                            if (!tc.weights.empty())
                                std::cout << "(Prob=" << tc.weights[i] << ')';
                        }
                        std::cout << '\n';
                    }
                }
                std::cout << "Raw source records only. Use treasure for single-player TC selection. "
                             "Use quality or loot-plan for the supported quality and consumable branches.\n";
            }
        } else if (command == "list") {
            for (auto &name : a.list(argc > 3 ? argv[3] : "*"))
                std::cout << name << '\n';
        } else if (command == "text" && argc == 4) {
            std::string member = d2x::normalize(argv[3]);
            if (!member.ends_with(".txt"))
                throw std::runtime_error("text requires an original MPQ .txt member");
            auto bytes = a.read(member);
            if (bytes.size() > 4 * 1024 * 1024)
                throw std::runtime_error("MPQ text member exceeds display limit");
            std::cout << d2x::decodeText(bytes);
        } else if (command == "hex" && (argc == 4 || argc == 5)) {
            size_t count = argc == 5 ? size_t(std::stoul(argv[4])) : 128;
            if (count > 512)
                throw std::runtime_error("hex byte-count must be 0..512");
            auto bytes = a.read(argv[3]);
            constexpr char digits[] = "0123456789abcdef";
            for (size_t index = 0; index < std::min(count, bytes.size()); ++index) {
                const auto value = bytes[index];
                std::cout << digits[value >> 4] << digits[value & 15]
                          << (index % 16 == 15 ? '\n' : ' ');
            }
            if (count && std::min(count, bytes.size()) % 16)
                std::cout << '\n';
        } else if (command == "ds1-paths" && argc == 4) {
            std::string member = d2x::normalize(argv[3]);
            if (!member.ends_with(".ds1"))
                throw std::runtime_error("ds1-paths requires an original MPQ DS1 member");
            auto map = d2x::decodeDs1(a.read(member), member);
            std::cout << member << " version=" << map.version << " objects=" << map.objects.size() << '\n';
            for (const auto &object : map.objects)
                {
                    std::cout << "  type=" << object.type << " id=" << object.id << " at="
                              << object.x << ',' << object.y << " nodes=" << object.path.size() << '\n';
                    for (const auto &node : object.path)
                        std::cout << "    " << node.x << ',' << node.y << " action=" << node.action << '\n';
                }
        } else if (command == "extract" && argc == 5) {
            auto b = a.read(argv[3]);
            d2x::writeFile(argv[4], b);
            std::cout << "Extracted " << b.size() << " bytes\n";
        } else if (command == "preview" && (argc >= 5 && argc <= 8)) {
            auto b = a.read(argv[3]);
            auto anim = d2x::normalize(argv[3]).ends_with(".dcc") ? d2x::decodeDcc(b) : d2x::decodeDc6(b);
            auto pal = d2x::decodePalette(a.read("data/global/palette/act1/pal.dat"));
            const int first = argc >= 6 ? std::stoi(argv[5]) : 0;
            if (first < 0 || first >= int(anim.frames.size()))
                throw std::runtime_error("Preview first frame is out of range");
            const int columns = argc >= 7 ? std::stoi(argv[6]) : 4;
            const int requested = argc >= 8 ? std::stoi(argv[7]) : 16;
            if (columns < 1 || columns > 32 || requested < 1 || requested > 128)
                throw std::runtime_error("Invalid preview layout");
            const int gap = argc >= 7 ? 0 : 8;
            int w = 1, h = 1, count = std::min(requested, int(anim.frames.size()) - first);
            for (int i = 0; i < count; i++) {
                w = std::max(w, anim.frames[first + i].width);
                h = std::max(h, anim.frames[first + i].height);
            }
            Image img = GenImageColor((w + gap) * columns, (h + gap) * ((count + columns - 1) / columns), {32, 32, 32, 255});
            for (int i = 0; i < count; i++) {
                auto &f = anim.frames[first + i];
                for (int y = 0; y < f.height; y++)
                    for (int x = 0; x < f.width; x++) {
                        auto p = pal[f.pixels[y * f.width + x]];
                        if (p.a)
                            ImageDrawPixel(&img, (i % columns) * (w + gap) + x, (i / columns) * (h + gap) + y,
                                           {p.r, p.g, p.b, p.a});
                    }
            }
            bool ok = ExportImage(img, argv[4]);
            UnloadImage(img);
            if (!ok)
                throw std::runtime_error("Preview export failed");
        } else if (command == "pack" && argc == 5) {
            std::ifstream in(argv[3]);
            if (!in)
                throw std::runtime_error("Cannot read manifest");
            std::string name;
            while (std::getline(in, name))
                if (!name.empty() && name[0] != '#')
                    a.read(name);
            a.packUsed(argv[4]);
            std::cout << "Packed " << a.used.size() << " members\n";
        } else
            throw std::runtime_error("Invalid command");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

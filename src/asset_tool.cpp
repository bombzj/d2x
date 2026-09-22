#include "content/classic_data.hpp"
#include "content/monster_catalog.hpp"
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include "world/maze.hpp"
#include "world/outdoor.hpp"
#include "world/population.hpp"
#include "world/world_report.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <raylib.h>

int main(int argc, char **argv) {
    try {
        if (argc < 3) {
            std::cout
                << "d2x_assets <archive-or-folder> list [wildcard]\n  ... extract <member> <destination>\n  "
                   "... preview <dc6-or-dcc-member> <sheet.png>\n  ... pack <manifest.txt> <new.mpq>\n"
                   "  ... item <code>\n  ... drops <monster-class>\n  ... maps [Act-I-level-ID]\n"
                   "  ... presets [name-filter]\n  ... maze <level-ID> [map-seed] [difficulty:0-2]\n"
                   "  ... outdoor <level-ID> [map-seed]\n"
                   "  ... population <level-ID> [normal|nightmare|hell] [seed]\n";
            return 0;
        }
        d2x::Archives a;
        a.mountDirectory(argv[1]);
        std::string command = argv[2];
        if ((command == "maze" || command == "outdoor") && argc >= 4 && argc <= 6) {
            d2x::WorldCatalog catalog(a);
            uint32_t seed = d2x::defaultMapSeed;
            if (argc >= 5) {
                std::string value = argv[4];
                auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seed);
                if (error != std::errc{} || end != value.data() + value.size())
                    throw std::runtime_error("Map seed expects uint32");
            }
            auto recipe =
                command == "maze"
                    ? d2x::generateMaze(catalog, std::stoi(argv[3]), seed, argc == 6 ? std::stoi(argv[5]) : 0)
                    : d2x::generateAct1Outdoors(a, catalog, seed).at(std::stoi(argv[3]));
            std::cout << recipe.ds1 << " rooms=" << recipe.pieces.size() << '\n';
            for (const auto &room : recipe.pieces)
                std::cout << "  room " << room.x << ',' << room.y << " size=" << room.width << 'x'
                          << room.height << " preset=" << room.preset << " variant=" << room.variant << " "
                          << room.ds1 << '\n';
            d2x::TileLibraryCache cache(a);
            d2x::Map map;
            map.load(a, cache, recipe);
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
            for (const auto &layer : map.data.walls)
                for (int y = 0; y < map.data.height; ++y)
                    for (int x = 0; x < map.data.width; ++x) {
                        const auto &cell = layer[y * map.data.width + x];
                        if (cell.occupied() && (cell.orientation == 10 || cell.orientation == 11))
                            std::cout
                                << "  warp marker " << x << ',' << y << " style=" << ((cell.value >> 20) & 63)
                                << " sequence=" << ((cell.value >> 8) & 255) << " type=" << cell.orientation
                                << " hidden=" << cell.hidden() << " tile=" << map.tileIndex(cell, x, y)
                                << '\n';
                    }
        } else if (command == "population" && argc >= 4 && argc <= 6) {
            d2x::PopulationSettings settings;
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
            map.load(a, cache, recipe);
            auto plan = d2x::planPopulation(monsters, &level, preset, map, settings);
            d2x::writePopulationReport(std::cout, plan, &level, preset, settings);
        } else if (command == "maps" && (argc == 3 || argc == 4)) {
            d2x::WorldCatalog catalog(a);
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
        } else if ((command == "item" || command == "drops") && argc == 4) {
            auto data = d2x::loadClassicData(a);
            std::cout << "Data profile: " << data.profile << '\n';
            if (command == "item") {
                auto item = data.items.find(argv[3]);
                if (!item)
                    throw std::runtime_error("Unknown item code");
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
                std::cout << "Raw source records only. Recursive selection, NoDrop execution and quality "
                             "generation remain unimplemented; monster loot is disabled.\n";
            }
        } else if (command == "list") {
            for (auto &name : a.list(argc > 3 ? argv[3] : "*"))
                std::cout << name << '\n';
        } else if (command == "extract" && argc == 5) {
            auto b = a.read(argv[3]);
            d2x::writeFile(argv[4], b);
            std::cout << "Extracted " << b.size() << " bytes\n";
        } else if (command == "preview" && argc == 5) {
            auto b = a.read(argv[3]);
            auto anim = d2x::normalize(argv[3]).ends_with(".dcc") ? d2x::decodeDcc(b) : d2x::decodeDc6(b);
            auto pal = d2x::decodePalette(a.read("data/global/palette/act1/pal.dat"));
            int w = 1, h = 1, count = std::min(16, int(anim.frames.size()));
            for (int i = 0; i < count; i++) {
                w = std::max(w, anim.frames[i].width);
                h = std::max(h, anim.frames[i].height);
            }
            Image img = GenImageColor((w + 8) * 4, (h + 8) * ((count + 3) / 4), {32, 32, 32, 255});
            for (int i = 0; i < count; i++) {
                auto &f = anim.frames[i];
                for (int y = 0; y < f.height; y++)
                    for (int x = 0; x < f.width; x++) {
                        auto p = pal[f.pixels[y * f.width + x]];
                        if (p.a)
                            ImageDrawPixel(&img, (i % 4) * (w + 8) + x, (i / 4) * (h + 8) + y,
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

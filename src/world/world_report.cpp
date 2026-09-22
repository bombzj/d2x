#include "world_report.hpp"
#include "cow_level.hpp"
#include "maze.hpp"
#include "outdoor.hpp"
#include <set>

namespace d2x {
void writeWorldReport(std::ostream &out, Archives &archives, const WorldCatalog &catalog, int selected) {
    std::set<std::string> allMissing;
    int count = 0, ready = 0;
    auto outdoor = outdoorMissing(archives, catalog).empty()
                       ? generateAct1Outdoors(archives, catalog, defaultMapSeed)
                       : std::map<int, MapRecipe>{};
    for (const auto &[id, level] : catalog.levels()) {
        if (level.act != 0 || (selected && id != selected))
            continue;
        ++count;
        auto available = catalog.availability(archives, id);
        if (outdoor.contains(id)) {
            available.recipe = outdoor.at(id);
            available.missing.clear();
            available.reason.clear();
        }
        if (supportsMaze(id)) {
            available.recipe = generateMaze(catalog, id, defaultMapSeed, 0);
            available.missing = mazeMissing(archives, catalog, id);
            available.reason.clear();
        }
        if (id == 39) {
            available.missing = cowLevelMissing(archives, catalog);
            available.reason.clear();
            if (available.missing.empty())
                available.recipe = generateCowLevel(archives, catalog, defaultMapSeed);
        }
        ready += available.ready();
        const char *kind = level.generation == GenerationKind::Preset ? "preset"
                           : level.generation == GenerationKind::Maze ? "maze"
                                                                      : "outdoor";
        out << id << " | " << level.name << " | " << kind << " | type=" << level.levelType
            << " size=" << level.width << 'x' << level.height << " | "
            << (available.ready()          ? (id == 39               ? "GENERATED COW TERRAIN / QUEST PORTAL PENDING"
                                              : supportsMaze(id)     ? "GENERATED MAZE READY"
                                              : outdoor.contains(id) ? "CONNECTED OUTDOOR READY"
                                                                     : "PRESET TERRAIN READY")
                : available.reason.empty() ? "MISSING RESOURCES"
                                           : available.reason)
            << '\n';
        for (int i = 0; i < 8; ++i)
            if (level.visible[i])
                out << "  Vis" << i << " -> " << level.visible[i] << " Warp" << i << '=' << level.warps[i]
                    << '\n';
        if (auto found = catalog.mazes().find(id); found != catalog.mazes().end()) {
            const auto &maze = found->second;
            out << "  LvlMaze: Rooms=" << maze.rooms << " Size=" << maze.width << 'x' << maze.height
                << " Merge=" << maze.merge << '\n';
        }
        for (const auto &[presetId, preset] : catalog.presets()) {
            if (preset.level != id)
                continue;
            out << "  LvlPrest Def=" << presetId << " Dt1Mask=" << preset.dt1Mask
                << " FillBlanks=" << preset.fillBlanks << " Populate=" << preset.populate << '\n';
            for (int variant = 0; variant < 6; ++variant) {
                if (preset.variants[variant].empty())
                    continue;
                auto recipe = catalog.preset(presetId, level.levelType, variant);
                auto missing = catalog.missing(archives, recipe);
                out << "    variant " << variant << " " << recipe.ds1 << " ["
                    << (missing.empty() ? "files present" : "missing resources") << "]\n";
                for (const auto &file : missing) {
                    out << "      MISSING " << file << '\n';
                    allMissing.insert(file);
                }
                if (selected)
                    for (const auto &file : recipe.tileLibraries)
                        out << "      DT1 " << file << '\n';
            }
        }
        for (const auto &file : available.missing) {
            out << "  MISSING " << file << '\n';
            allMissing.insert(file);
        }
    }
    if (!count)
        throw std::runtime_error("Unknown Act I level");
    out << "\n"
        << ready << '/' << count << " levels have supported terrain and its DS1/DT1 files (variant 0).\n"
        << "Levels 1..37 form the implemented Act I exploration route when all resources are present.\n"
        << "Cow terrain uses four secondary border substitutions. Main-route terrain has three secondary "
           "border passes, river/bridge presets and native dirt floor tiles with adapted path routing.\n"
          << "Cliff contours, cliff entrances and town transition presets are implemented; full LvlSub "
              "themes, native path routing and quest portals remain incomplete. Terrain availability is "
              "not full Act I feature parity.\n"
        << "Missing " << allMissing.size()
        << " known files. Maze/outdoor lists cover LevelType DT1s; exact DS1 demand depends on unimplemented "
           "generation.\n";
    for (const auto &diagnostic : catalog.diagnostics())
        out << "NOTE " << diagnostic << '\n';
}
} // namespace d2x

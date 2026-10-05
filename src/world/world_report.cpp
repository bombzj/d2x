#include "world_report.hpp"
#include "cow_level.hpp"
#include "maze.hpp"
#include "world/outdoor/outdoor.hpp"
#include "region.hpp"
#include "core/random_seed.hpp"
#include <set>

namespace d2x {
void writeWorldReport(std::ostream &out, Archives &archives, const WorldCatalog &catalog, int selected) {
    const auto seed = freshSeed();
    out << "World report seed=" << seed << '\n';
    std::set<std::string> allMissing;
    int count = 0, ready = 0;
    WorldSelection selection;
    selection.seed = seed;
    const auto plan = planWorld(archives, catalog, selection);
    for (const auto &[id, level] : catalog.levels()) {
        if (selected && id != selected)
            continue;
        ++count;
        LevelAvailability available;
        const auto entry = std::find_if(plan.entries.begin(), plan.entries.end(),
            [&](const auto &value) { return value.level == id; });
        const auto region = std::find_if(plan.regions.begin(), plan.regions.end(),
            [&](const auto &value) { return int(value.definition.id) == id; });
        if (region != plan.regions.end()) {
            available.recipe = region->recipe;
            available.missing = catalog.missing(archives, region->recipe);
        } else if (entry != plan.entries.end()) {
            available.reason = entry->status;
            available.missing = entry->missing;
        } else available.reason = "No runtime world entry";
        ready += available.ready();
        const char *kind = level.generation == GenerationKind::Preset ? "preset"
                           : level.generation == GenerationKind::Maze ? "maze"
                                                                      : "outdoor";
        out << id << " | " << level.name << " | " << kind << " | type=" << level.levelType
            << " size=" << level.width << 'x' << level.height << " | "
            << (available.ready()          ? (id == 39               ? "GENERATED COW TERRAIN / QUEST PORTAL PENDING"
                                              : supportsMaze(id)     ? "GENERATED MAZE READY"
                                              : level.generation == GenerationKind::Outdoor ? "GENERATED OUTDOOR TERRAIN READY"
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
        throw std::runtime_error("Unknown level");
    out << "\n"
        << ready << '/' << count << " levels have supported terrain and its DS1/DT1 files (variant 0).\n"
        << "Levels 1..37 form the implemented Act I exploration route when all resources are present.\n"
        << "Act I uses the shared native room generator for layout, DT1 variants, collision and preset units.\n"
        << "Terrain availability does not certify combat, quests or the other acts' native map parity.\n"
        << "Missing " << allMissing.size()
        << " known files. Maze/outdoor lists cover LevelType DT1s; exact DS1 demand depends on unimplemented "
           "generation.\n";
    for (const auto &diagnostic : catalog.diagnostics())
        out << "NOTE " << diagnostic << '\n';
}
} // namespace d2x

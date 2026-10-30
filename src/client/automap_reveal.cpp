#include "automap_reveal.hpp"
#include "content/world/automap_data.hpp"
#include "world/native_map.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace d2x {
struct AutomapReveal::Result {
    struct Level {
        int layer{};
        size_t roomsTotal{}, roomsCompleted{};
        AutomapCells cells;
        std::optional<AutomapDrawView::Town> town;
        struct Exit { int destination{}, x{}, y{}; auto operator<=>(const Exit &) const = default; };
        std::set<Exit> exits;
        bool complete{};
    };
    int root{};
    std::map<int, Level> levels;
    size_t borrowed{}, released{};
    bool complete{}, failed{};
    std::string reason;
};
struct AutomapReveal::Job {
    TileLibraryCache libraries;
    std::shared_ptr<const WorldCatalog> catalog;
    std::unique_ptr<NativeMapGenerator> generator;
    AutomapPresetCellSelector presetCell{};
    std::set<int> pending;
    std::map<int, std::vector<size_t>> rooms;
    bool planning{true};
    Job(Archives &a, AutomapRevealIdentity id, int root, AutomapPresetCellSelector selector)
        : libraries(a), catalog(sharedWorldCatalog(a, id.difficulty)),
          generator(std::make_unique<NativeMapGenerator>(a, *catalog, libraries, id.act, id.seed, id.difficulty)),
          presetCell(selector), pending{root} {}
};
AutomapReveal::AutomapReveal(Archives &a, const AutomapCatalog &catalog, size_t count)
    : archives_(a), automap_(catalog), cellCount_(count) {}
AutomapReveal::~AutomapReveal() = default;
void AutomapReveal::reset() {
    if(current_) ++revision_;
    job_.reset(); current_.reset(); completed_.clear(); identity_.reset(); progress_ = {};
}
void AutomapReveal::disable() {
    // A cancelled partial job restarts from native generation on re-enable;
    // only fully exported components are reusable without their room session.
    if(current_) ++revision_;
    job_.reset(); current_.reset(); progress_ = {};
}
void AutomapReveal::refreshProgress() {
    progress_ = {};
    if (!current_) return;
    progress_.enabled = true;
    progress_.rootLevel = current_->root;
    progress_.state = current_->failed ? "partial" : current_->complete ? "complete" : "running";
    progress_.reason = current_->reason;
    progress_.borrowed = current_->borrowed; progress_.released = current_->released;
    for (const auto &[id, level] : current_->levels) {
        size_t cells{};
        for (const auto &group : level.cells) cells += group.size();
        progress_.levels.push_back({id, level.layer, level.roomsCompleted, level.roomsTotal,
            cells, level.cells[2].size(), level.exits.size(), level.complete});
        progress_.roomsCompleted += level.roomsCompleted;
        progress_.roomsTotal += level.roomsTotal;
    }
    if (job_ && job_->generator)
        for (const auto &room : job_->generator->tiles().rooms()) progress_.activeRooms += room.loaded;
}
void AutomapReveal::update(AutomapRevealIdentity identity, int root, AutomapPresetCellSelector selector) {
    if (identity_ != identity) { reset(); identity_ = identity; }
    if (const auto found = completed_.find(root); found != completed_.end()) {
        if(current_!=found->second) ++revision_;
        job_.reset(); current_ = found->second; refreshProgress(); return;
    }
    if (!current_ || (!current_->levels.contains(root) && current_->root != root)) {
        job_.reset();
        current_ = std::make_shared<Result>(); current_->root = root;
        ++revision_;
        try { job_ = std::make_unique<Job>(archives_, identity, root, selector); }
        catch (const std::exception &e) { current_->failed = true; current_->reason = e.what(); }
    }
    if (!job_ || current_->failed) { refreshProgress(); return; }
    // A room export may append cells before failing; invalidate consumers
    // before either successful or partial output can become visible.
    ++revision_;
    try {
        auto &job = *job_;
        auto &generator = *job.generator;
        if (job.planning) {
            const int id = *job.pending.begin(); job.pending.erase(job.pending.begin());
            const auto &record = job.catalog->level(id);
            if (record.act != identity.act || record.automapLayer < 0)
                throw std::runtime_error("Automap walking component has an invalid act or layer");
            auto rooms = generator.levelRooms(id);
            auto &level = current_->levels[id];
            level.layer = record.automapLayer; level.roomsTotal = rooms.size();
            if (rooms.empty()) throw std::runtime_error("Automap level has no native rooms");
            // BH walks the native prepend Room2 list, not creation order.
            std::reverse(rooms.begin(), rooms.end()); job.rooms.emplace(id, std::move(rooms));
            if (id == 40 || id == 103 || id == 109) {
                const auto recipe = generator.recipe(id);
                const auto &p = generator.layout().levels.at(id);
                level.town = AutomapDrawView::Town{id, recipe.variant,
                    {float(p.x * 5 + p.width * 5 / 2), float(p.y * 5 + p.height * 5 / 2)}};
            }
            const auto found = generator.layout().connections.find(id);
            const auto slots = found == generator.layout().connections.end()
                ? NativeActLayout::ConnectionSlots{record.visible, record.warps} : found->second;
            for (size_t slot = 0; slot < slots.visible.size(); ++slot) {
                const int next = slots.visible[slot];
                if (next && slots.warps[slot] == -1 && job.catalog->level(next).act == identity.act &&
                    !current_->levels.contains(next)) job.pending.insert(next);
            }
            job.planning = !job.pending.empty();
        } else {
            auto at = std::find_if(current_->levels.begin(), current_->levels.end(),
                [](const auto &entry) { return !entry.second.complete; });
            if (at == current_->levels.end()) {
                const bool artwork = std::ranges::any_of(current_->levels, [](const auto &entry) {
                    return entry.second.town || std::ranges::any_of(entry.second.cells,
                        [](const auto &cells) { return !cells.empty(); });
                });
                if (!artwork)
                    throw std::runtime_error("Current MPQ automap rules produced no artwork for this walking component");
                current_->complete = true;
                for (const auto &[id, level] : current_->levels) { (void)level; completed_[id] = current_; }
                job_.reset();
            } else {
                const int id = at->first;
                auto &level = at->second;
                const auto index = job.rooms.at(id).at(level.roomsCompleted);
                const bool borrowed = generator.visitAutomapRoom(index, [&](const auto &tiles, size_t roomIndex) {
                    const auto &room = tiles.rooms().at(roomIndex);
                    const auto &record = job.catalog->level(id);
                    const auto &placement = generator.layout().levels.at(id);
                    const auto add = [&](int group, int cel, int x, int y) {
                        if (cel < 0 || size_t(cel) >= cellCount_ || cel > UINT16_MAX) return;
                        if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX)
                            throw std::runtime_error("Revealed automap cell exceeds native coordinates");
                        insertNativeCell(level.cells[size_t(group)], {uint16_t(cel), int16_t(x), int16_t(y)});
                    };
                    const auto terrain = [&](const std::vector<size_t> &indices, int group) {
                        for (const auto tileIndex : indices) {
                            const auto &tile = tiles.tiles().at(tileIndex);
                            if (tile.type == 13 || tile.type == 15 || (tile.flags & 8)) continue;
                            const MapCell cell{uint32_t((tile.tile->main << 20) | (tile.tile->sub << 8)), tile.type};
                            for (const int cel : automap_.cellCels(record.levelType, cell,
                                tile.x - placement.x, tile.y - placement.y))
                                add(group, cel, (tile.x - tile.y) * 8,
                                    (tile.x + tile.y) * 4 + (tile.type >= 16 ? 24 : 0));
                        }
                    };
                    terrain(room.floors, 0); terrain(room.walls, 1);
                    std::set<std::tuple<int, int, int, int>> seen;
                    const auto preset = [&](int type, int classId, int x, int y) {
                        if (!seen.emplace(type, classId, x, y).second) return;
                        if (type == 5) {
                            for (const auto &warp : room.warps)
                                if (warp.id == classId && warp.destination)
                                    level.exits.insert({tiles.rooms().at(*warp.destination).level, x, y});
                        }
                        const int cel = job.presetCell ? job.presetCell(id, type, classId, automap_)
                            : type == 2 ? automap_.objectCel(classId) : -1;
                        add(2, cel, ((x - y) * 16) / 10 + 1, ((x + y) * 8) / 10 - 3);
                    };
                    for (const auto &unit : room.authoredUnits)
                        if (!unit.clientOnly && unit.identityResolved)
                            preset(unit.unit.type, unit.unit.id, unit.unit.x, unit.unit.y);
                    for (const auto &unit : room.units) preset(unit.type, unit.id, unit.x, unit.y);
                });
                current_->borrowed += borrowed; current_->released += borrowed;
                ++level.roomsCompleted; level.complete = level.roomsCompleted == level.roomsTotal;
            }
        }
    } catch (const std::exception &e) {
        current_->failed = true; current_->reason = e.what(); job_.reset();
    }
    refreshProgress();
}
void AutomapReveal::appendCells(int layer, AutomapCells &cells) const {
    if (!current_) return;
    for (const auto &[id, level] : current_->levels) {
        (void)id;
        if (level.layer != layer) continue;
        for (size_t group = 0; group < cells.size(); ++group)
            for (const auto &cell : level.cells[group]) insertNativeCell(cells[group], cell);
    }
}
void AutomapReveal::appendTowns(int layer, std::vector<AutomapDrawView::Town> &towns) const {
    if (!current_) return;
    for (const auto &[id, level] : current_->levels)
        if (level.layer == layer && level.town &&
            std::ranges::none_of(towns, [&](const auto &town) { return town.level == id; }))
            towns.push_back(*level.town);
}
}

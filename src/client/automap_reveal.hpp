#pragma once
#include "automap_cells.hpp"
#include "contracts/automap_reveal.hpp"
#include "contracts/map.hpp"
#include <map>
#include <memory>
#include <optional>

namespace d2x {
class Archives;
class AutomapCatalog;
// The caller owns preset policy; the map service owns generation and lifetime.
using AutomapPresetCellSelector = int (*)(int level, int type, int id, const AutomapCatalog &);
struct AutomapRevealIdentity {
    uint64_t gameGeneration{}, areaGeneration{};
    uint32_t seed{}, objectSeed{};
    int act{}, difficulty{};
    auto operator<=>(const AutomapRevealIdentity &) const = default;
};
class AutomapReveal {
    struct Result;
    struct Job;
    Archives &archives_;
    const AutomapCatalog &automap_;
    size_t cellCount_{};
    std::optional<AutomapRevealIdentity> identity_;
    std::map<int, std::shared_ptr<Result>> completed_;
    std::shared_ptr<Result> current_;
    std::unique_ptr<Job> job_;
    AutomapRevealProgress progress_;
    void refreshProgress();
  public:
    AutomapReveal(Archives &, const AutomapCatalog &, size_t cellCount);
    ~AutomapReveal();
    void reset();
    void disable();
    // Current level plus resolved Vis links with Warp=-1; no stairs/portals.
    // One level allocation or one room export per call, independent of Tab.
    void update(AutomapRevealIdentity, int level, AutomapPresetCellSelector);
    const AutomapRevealProgress &progress() const { return progress_; }
    void appendCells(int layer, AutomapCells &) const;
    void appendTowns(int layer, std::vector<AutomapDrawView::Town> &) const;
};
}

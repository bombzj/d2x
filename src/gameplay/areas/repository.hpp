#pragma once
#include "gameplay/areas/state.hpp"
#include <span>
#include <utility>

namespace d2x {
// Parked areas are moved into/out of the simulator, never copied per observer.
class AreaRepository {
    std::vector<AreaState> parked_;
  public:
    void reset(std::span<const RegionId> ids) {
        std::vector<AreaState> next(ids.size());
        for (size_t index = 0; index < ids.size(); ++index) next[index].region = ids[index];
        parked_.swap(next);
    }
    void replace(std::vector<AreaState> next) { parked_.swap(next); }
    const AreaState &read(size_t slot) const { return parked_.at(slot); }
    std::span<const AreaState> parked() const { return parked_; }
    bool initialized(size_t slot) const { return read(slot).initialized; }
    void park(size_t slot, AreaState area) { parked_.at(slot) = std::move(area); }
    AreaState take(size_t slot) {
        AreaState empty;
        empty.region = read(slot).region;
        return std::exchange(parked_.at(slot), std::move(empty));
    }
};
} // namespace d2x

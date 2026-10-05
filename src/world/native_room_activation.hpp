#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace d2x {
// DRLG room references are separate from gameplay monster activation and from
// the set of server 07 packets. No MPQ, renderer or transport dependency.
class NativeRoomActivation {
  public:
    struct State {
        uint8_t status{4};
        std::array<uint16_t, 4> references{};
        bool prepared{}, active{};
    };
    struct Operations {
        std::function<std::vector<size_t>(size_t)> near;
        std::function<void(size_t)> prepare; // DT1 libraries / lazy preset units.
        std::function<void(size_t)> create;  // Reset tile seed, grids, DT1, collision.
        std::function<void(size_t)> release; // Client tile/collision removal.
    };
  private:
    Operations operations_;
    std::map<size_t, State> states_;
    bool failed_{};
    void setStatus(size_t, uint8_t);
    void unsetStatus(size_t, uint8_t);
    void propagateSet(size_t, uint8_t);
    void propagateUnset(size_t, uint8_t);
    void addReference(size_t, uint8_t);
    void removeReference(size_t, uint8_t);
    void setRoot(size_t, uint8_t);
    void unsetRoot(size_t, uint8_t);
  public:
    explicit NativeRoomActivation(Operations);
    void initialize(size_t); // Automap initialization, without sight references.
    // Idempotence follows the native in-sight reference guard, not a second
    // artificial packet counter. Failure requires a fresh generation session.
    void reveal(size_t);
    void hide(size_t);
    void changeClientRoom(std::optional<size_t> previous, std::optional<size_t> next);
    const auto &states() const { return states_; }
    bool valid() const { return !failed_; }
};
} // namespace d2x

#include "native_room_activation.hpp"
#include <limits>
#include <stdexcept>
#include <utility>

// D2MOO DrlgActivate, MIT: docs/licenses/D2MOO.txt. Client 07/08 dispatch
// verified statically in 1.13c D2Client (MD5 f5860c629d309b8fc1f96174babcf633),
// RVAs 0xac440 / 0xac3d0 -> D2Common ordinals 10401 / 11099. These are native
// in-sight reference operations, including propagation before root activation.
namespace d2x {
NativeRoomActivation::NativeRoomActivation(Operations operations) : operations_(std::move(operations)) {
    if (!operations_.near || !operations_.prepare || !operations_.create || !operations_.release)
        throw std::invalid_argument("Native room activation requires complete lifecycle operations");
}
void NativeRoomActivation::setStatus(size_t room, uint8_t status) {
    auto &state = states_[room];
    if (status == 3) {
        if (!state.prepared) { operations_.prepare(room); state.prepared = true; }
    } else if (status == 2) {
        if (!state.prepared) return;
    } else if (status == 1 && !state.active) {
        operations_.create(room);
        state.active = true;
    }
    const bool changed = state.status > status;
    if (changed) state.status = status;
    if (status == 2 && changed && state.references[1]) setStatus(room, 1);
}
void NativeRoomActivation::unsetStatus(size_t room, uint8_t status) {
    auto &state = states_.at(room);
    if (state.status >= 4 || !state.references[state.status]) {
        state.status = 4;
        for (uint8_t i = 0; i < 4; ++i)
            if (state.references[i]) { state.status = i; break; }
    }
    if (status == 3 && state.status == 4) {
        operations_.release(room);
        // FreeRoom removes grids/tiles, while TILELIB_LOADED and
        // PRESET_UNITS_ADDED survive. Reentry must not repeat lazy unit RNG.
        state.active = false;
    }
}
void NativeRoomActivation::addReference(size_t room, uint8_t status) {
    auto &state = states_[room];
    bool earlier = false;
    for (uint8_t i = 0; i <= status; ++i) earlier |= state.references[i] != 0;
    if (state.status >= status && !earlier) setStatus(room, status);
    auto &count = state.references[status];
    if (count == std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("Native room reference count overflow");
    ++count;
}
void NativeRoomActivation::removeReference(size_t room, uint8_t status) {
    auto &state = states_.at(room);
    auto &count = state.references[status];
    if (!count) throw std::runtime_error("Unbalanced native room reference release");
    --count;
    unsetStatus(room, status);
}
void NativeRoomActivation::propagateSet(size_t room, uint8_t status) {
    const auto near = operations_.near(room);
    for (const auto other : near) {
        if (status + 1 < 4) propagateSet(other, status + 1);
        addReference(other, status);
    }
}
void NativeRoomActivation::propagateUnset(size_t room, uint8_t status) {
    const auto near = operations_.near(room);
    for (const auto other : near) {
        removeReference(other, status);
        if (status + 1 < 4) propagateUnset(other, status + 1);
    }
}
void NativeRoomActivation::setRoot(size_t room, uint8_t status) {
    propagateSet(room, status + 1);
    addReference(room, status);
}
void NativeRoomActivation::unsetRoot(size_t room, uint8_t status) {
    const auto found = states_.find(room);
    if (found == states_.end() || !found->second.references[status]) return;
    removeReference(room, status);
    const auto near = operations_.near(room);
    for (const auto other : near) {
        removeReference(other, status + 1);
        if (status + 2 < 4) propagateUnset(other, status + 2);
    }
}
void NativeRoomActivation::reveal(size_t room) {
    if (failed_) throw std::runtime_error("Discard failed native room activation session");
    if (states_[room].references[1]) return;
    failed_ = true;
    setRoot(room, 1);
    failed_ = false;
}
void NativeRoomActivation::initialize(size_t room) {
    // Internal level-generation callbacks may initialize automap rooms while
    // sight propagation is in progress. Preserve the enclosing failure guard.
    const bool enclosingOperation = failed_;
    failed_ = true;
    auto &state = states_[room];
    if (!state.prepared) { operations_.prepare(room); state.prepared = true; }
    if (!state.active) { operations_.create(room); state.active = true; }
    failed_ = enclosingOperation;
}
void NativeRoomActivation::hide(size_t room) {
    if (failed_) throw std::runtime_error("Discard failed native room activation session");
    failed_ = true;
    unsetRoot(room, 1);
    failed_ = false;
}
void NativeRoomActivation::changeClientRoom(std::optional<size_t> previous, std::optional<size_t> next) {
    if (failed_) throw std::runtime_error("Discard failed native room activation session");
    if (previous == next) return;
    failed_ = true;
    if (next) setRoot(*next, 0);
    if (previous) unsetRoot(*previous, 0);
    failed_ = false;
}
} // namespace d2x

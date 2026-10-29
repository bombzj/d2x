#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/items/handle.hpp"
#include <array>
#include <optional>
#include <vector>

namespace d2x {
struct InputSkillSelection {
    int skill = -1;
    EntityId source;
    bool operator==(const InputSkillSelection &) const = default;
};
// One frame's hit/projection results. No device, authority or GPU ownership.
struct WorldInputView {
    uint64_t gameGeneration{}, areaGeneration{};
    bool available = false, movementAvailable = false;
    // A held unit cast can continue when the pointer leaves the world surface.
    bool targetingAvailable = false;
    bool castInputBusy = false; // Existing local approach/action clock; channel updates remain allowed.
    Vec origin, size, observer, point; // Original global subtiles; observer is display-only.
    EntityId interaction, combat, doorHighlight;
    std::optional<ItemHandle> pickup;
    std::array<std::optional<InputSkillSelection>, 2> skills;
    std::array<std::vector<EntityId>, 2> validCombatTargets;
};
} // namespace d2x

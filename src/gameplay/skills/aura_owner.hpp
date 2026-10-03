#pragma once
#include "core/id.hpp"
#include "gameplay/skills/aura.hpp"
#include <optional>

namespace d2x {
// Borrowed emission ownership; the authority retains the only active instance.
struct SkillAuraOwner {
    EntityId id;
    const bool &dead;
    std::optional<ActiveAura> &aura;
};
} // namespace d2x

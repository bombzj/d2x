#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/effects/definition.hpp"
#include <optional>

namespace d2x::server::effects {
inline constexpr uint32_t conversionTimerHandler=1;
inline constexpr uint32_t nativeUnitSkillTimerHandler=1; // Scoped to PeriodicSkills.
enum class NativeSkillStateProcedure { FingerMageSpider,Infection,Warcry,BattleCry,SpiderGoo };
struct NativeFear {
    EntityId target,owner;
    CombatStateDefinition state;
    int skill{},rank{1},frames{};
    bool utility{true};
    std::optional<Vec> origin; // Global coordinates only for a missile target.
};
}

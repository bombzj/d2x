#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/effects/definition.hpp"
#include <optional>
#include <array>
#include <utility>

namespace d2x::server::effects {
inline constexpr uint32_t conversionTimerHandler=1;
inline constexpr uint32_t nativeUnitSkillTimerHandler=1; // Scoped to PeriodicSkills.
enum class NativeSkillStateProcedure { FingerMageSpider,Infection,Warcry,BattleCry,SpiderGoo,Pd2Debuff };
struct NativeSkillStateValues {
    int state{-1};
    std::array<std::pair<int,int32_t>,2> stats{{{-1,0},{-1,0}}};
};
struct NativeFear {
    EntityId target,owner;
    CombatStateDefinition state;
    int skill{},rank{1},frames{};
    bool utility{true};
    std::optional<Vec> origin; // Global coordinates only for a missile target.
};
}

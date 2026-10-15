#pragma once
#include "gameplay/effects/definition.hpp"

namespace d2x {
enum class NativeConversionProcedure { Paladin,MindBlast };
struct NativeConversionSpec {
    CombatStateDefinition state,saveState;
    int frames{};
    NativeConversionProcedure procedure{NativeConversionProcedure::Paladin};
};
}

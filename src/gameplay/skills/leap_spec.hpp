#pragma once
#include "gameplay/effects/definition.hpp"
namespace d2x {
struct LeapSpec {
    bool attack = false;
    int rangeMinimum{},rangeMaximum{},range{};
    int knockbackRadius{},knockbackPerLevel{};
    CombatStateDefinition moving;
};
}

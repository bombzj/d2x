#pragma once
#include <set>

namespace d2x {
// Corpse eligibility and retained death states travel together through room retirement.
struct CorpseState {
    bool unavailable{}, searched{};
    std::set<int> states;
};
}

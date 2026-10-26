#pragma once
#include "core/id.hpp"
#include <vector>

namespace d2x::server::monsters {
struct Pd2ScenePack {
    int phase{},remaining{-1};
    EntityId leader,champion;
    std::vector<EntityId> minions;
    std::vector<int> modifiers;
    bool done() const {return phase==7;}
};
}

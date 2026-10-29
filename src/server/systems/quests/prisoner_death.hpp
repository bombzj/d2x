#pragma once
#include "core/id.hpp"
#include <set>

namespace d2x::server::quests {
// Nodes are allocated while preparing a death. The accepted actor publication
// transfers them into A5Q2 without allocating or rerunning the room census.
struct PrisonerDeathPublication {
    EntityId unit;
    bool door{};
    std::set<EntityId> freed,deaths,doors;
};
}

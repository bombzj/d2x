#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include <optional>
#include <vector>

namespace d2x::server::monsters {
struct Actor;
struct Admission {
    MonsterIdentity identity;
    std::optional<MonsterKind> implementation;
    RegionId area;
    Vec position;
    bool hostile{};
    std::optional<MonsterRule> rule;
    std::vector<Vec> skillPositions{};
    uint64_t tick{};
};
}
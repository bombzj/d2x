#pragma once
#include "server/runtime/contracts.hpp"

namespace d2x::server::actions {
struct MonsterCastRequest {
    EntityId actor;
    uint16_t skill{};
    ActionTarget target;
    uint64_t tick{};
    uint8_t monsterMode{4};
    std::optional<Vec> position{};
    int teleportHeal{};
};
}
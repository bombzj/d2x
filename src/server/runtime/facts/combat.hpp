#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "server/game_commands.hpp"
#include <optional>
#include <cstdint>

namespace d2x::server {
struct DeathFact { uint64_t occurrence{}; EntityId victim, killer; RegionId area; uint32_t goldLost{}; };
struct AttackFact { EntityId actor, target; uint8_t actorType{}, targetType{}; RegionId area; Vec position, destination; uint64_t action{}; uint16_t skill{}; uint8_t rank{1}; bool forced{}; uint8_t monsterMode{4}; };
// Native life scale: type 0 uses 100, type 1 uses raw 128. Monster 0C bit 7
// carries the lightning-enchanted emission flag, independently of rank.
struct HitFact { EntityId target; uint8_t type{}; RegionId area; uint8_t life{}; bool killed{}; Vec position; uint8_t hitClass{}, monsterMode{}; std::optional<Vec> knockback{}; bool lightningReady{}; uint8_t playerMode{}; std::optional<MotionTarget> actionTarget{}; };
}

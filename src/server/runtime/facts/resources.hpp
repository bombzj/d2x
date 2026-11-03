#pragma once
#include "core/entity_id.hpp"
#include "gameplay/units/resources.hpp"

namespace d2x::server {
struct LifeFact { EntityId actor; Fixed8 life; };
struct ManaFact { EntityId actor; Fixed8 mana; };
// Resource-only commits do not clone equipment/passive graphs into output.
// Before/after values keep native delta encoding independent of later writes.
struct ResourcesFact { EntityId actor; ResourceValues before,after; };
}

#pragma once
#include "core/entity_id.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <cstdint>

namespace d2x::server {
struct SkillPulseFact { EntityId owner, target; uint8_t ownerType{}, targetType{1}; RegionId area; int skill{}, rank{}; Vec position; uint8_t parameter{};uint32_t argumentX{},argumentY{}; };
struct SkillEndFact {EntityId actor;uint8_t type{};RegionId area;uint16_t skill{};};
struct ItemSkillFact { EntityId owner,target; uint8_t targetType{1}; RegionId area; int skill{},rank{}; Vec position; uint16_t flags{}; uint8_t ownerType{}; };
struct MissileFact { EntityId owner; uint8_t ownerType{}; RegionId area; int definition{}, rank{}, frame{}; Vec position, destination; uint8_t pierce{}; EntityId missile{}; bool forcePublication{}; };
}

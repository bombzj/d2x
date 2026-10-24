#pragma once
#include "core/id.hpp"
#include <set>
namespace d2x {struct PersistentCharacter;}
namespace d2x::server::attributes {
int pd2EquippedEthereals(const PersistentCharacter &,const std::set<EntityId> &active);
}

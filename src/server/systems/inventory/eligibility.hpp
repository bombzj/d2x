#pragma once
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/items/operations.hpp"
namespace d2x::server::inventory {
// Draft-only update. Coalesces with existing item edits and never writes live state.
void synchronizeEquipment(PersistentCharacter &, const attributes::Totals &, const EquipmentRules &, std::vector<ItemChange> &);
}

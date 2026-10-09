#pragma once
#include "server/systems/attributes/calculation.hpp"
#include "gameplay/items/operations.hpp"
#include "server/player_state.hpp"
#include "server/runtime/contracts.hpp"
namespace d2x::server::inventory {
DomainStatus canReceiveQuestItem(const PlayerState &, const ItemDefinition &, const ItemCatalog &, unsigned difficulty);
// Draft-only update. Coalesces with existing item edits and never writes live state.
void synchronizeEquipment(PersistentCharacter &, const attributes::Totals &, const EquipmentRules &, std::vector<ItemChange> &);
}

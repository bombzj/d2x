#pragma once
#include "content/classic_data.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/items/inventory.hpp"

namespace d2x {
// Resolves only Properties.func1=1 direct instance rolls. Rebuild from base
// attributes so an item's own bonus cannot satisfy its own requirement.
CharacterModifiers resolveEquipmentModifiers(const ClassicData &content,
                                              const InventoryService &inventory,
                                              const PlayerContainers &containers,
                                              const EquipmentActor &baseActor,
                                              EntityId excludedItem = {});
} // namespace d2x

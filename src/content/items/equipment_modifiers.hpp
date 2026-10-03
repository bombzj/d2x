#pragma once
#include "gameplay/character/attributes.hpp"
#include "core/id.hpp"

namespace d2x {
struct ClassicData;
class InventoryService;
struct PlayerContainers;
struct EquipmentActor;
// Prepare definition facts after loading setItems, before publishing content.
void prepareEquipmentSetData(ClassicData &content);
// Adapt content property resolution to pure equipment contribution rules.
CharacterModifiers resolveEquipmentModifiers(const ClassicData &content,
                                              const InventoryService &inventory,
                                              const PlayerContainers &containers,
                                              const EquipmentActor &baseActor,
                                              EntityId excludedItem = {});
} // namespace d2x

#pragma once
#include "gameplay/character/persistent_character.hpp"
#include "core/bytes.hpp"
#include <vector>
namespace d2x {
struct ClassicData;
namespace server { struct InventoryFact; }
// Original inventory admission/delta packets; never exposes D2S bytes to UI.
std::vector<Bytes> nativePublicEquipment(const ClassicData &, PersistentCharacter);
std::vector<Bytes> nativeInventoryPackets(const ClassicData &, const PersistentCharacter &);
std::vector<Bytes> nativeInventoryDelta(const ClassicData &, const server::InventoryFact &);
}

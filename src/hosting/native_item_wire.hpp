#pragma once
#include "gameplay/character/persistent_character.hpp"
#include "core/bytes.hpp"
#include <vector>
namespace d2x {
struct ClassicData;
std::vector<Bytes> nativeShopItems(const ClassicData &, const PersistentCharacter &);
namespace server { struct InventoryFact; }
// Original inventory admission/delta packets; never exposes D2S bytes to UI.
std::vector<Bytes> nativeCorpseEquipment(const ClassicData &, PersistentCharacter);
enum class GroundItemAction : uint8_t { Add = 0, Drop = 2 };
Bytes nativeGroundItem(const ClassicData &, const ItemInstance &, Vec origin,
                       GroundItemAction = GroundItemAction::Add);
std::vector<Bytes> nativeMonsterEquipment(const ClassicData &, PersistentCharacter,EntityId,bool conceal = true);
std::vector<Bytes> nativePublicEquipment(const ClassicData &, PersistentCharacter);
std::vector<Bytes> nativeInventoryPackets(const ClassicData &, const PersistentCharacter &);
std::vector<Bytes> nativeInventoryDelta(const ClassicData &, const server::InventoryFact &);
}

#pragma once
#include "gameplay/character/persistent_character.hpp"
#include "core/bytes.hpp"
#include "native_identity.hpp"
#include <vector>
namespace d2x {
struct ClassicData;
std::vector<Bytes> nativeShopItems(const ClassicData &, const PersistentCharacter &);
namespace server { struct InventoryFact; struct TradeItemsFact; }
// Original inventory admission/delta packets; never exposes D2S bytes to UI.
std::vector<Bytes> nativeCorpseEquipment(const ClassicData &, PersistentCharacter);
enum class GroundItemAction : uint8_t { Add = 0, Drop = 2 };
Bytes nativeGroundItem(const ClassicData &, const ItemInstance &, Vec origin,
                       GroundItemAction = GroundItemAction::Add);
std::vector<Bytes> nativeMonsterEquipment(const ClassicData &, PersistentCharacter,EntityId,bool conceal = true);
std::vector<Bytes> nativePublicEquipment(const ClassicData &, const PersistentCharacter &);
std::vector<Bytes> nativePublicUnequip(const ClassicData &, const PersistentCharacter &, const ItemInstance &);
std::vector<Bytes> nativeInventoryPackets(const ClassicData &, const PersistentCharacter &);
std::vector<Bytes> nativeInventoryDelta(const ClassicData &, const server::InventoryFact &);
std::vector<Bytes> nativeTradeItems(const ClassicData &, const server::TradeItemsFact &, EntityId receiver);
PersistentCharacter nativeInventoryIdentity(PersistentCharacter,const hosting::NativeIdentityView &,uint8_t ownerType=0);
std::vector<Bytes> nativeShopItems(const ClassicData &,const PersistentCharacter &,const hosting::NativeIdentityView &);
std::vector<Bytes> nativeCorpseEquipment(const ClassicData &,PersistentCharacter,const hosting::NativeIdentityView &);
Bytes nativeGroundItem(const ClassicData &,const ItemInstance &,Vec,GroundItemAction,const hosting::NativeIdentityView &);
std::vector<Bytes> nativeMonsterEquipment(const ClassicData &,PersistentCharacter,EntityId,bool,const hosting::NativeIdentityView &);
std::vector<Bytes> nativePublicEquipment(const ClassicData &,const PersistentCharacter &,const hosting::NativeIdentityView &);
std::vector<Bytes> nativePublicUnequip(const ClassicData &,const PersistentCharacter &,const ItemInstance &,const hosting::NativeIdentityView &);
std::vector<Bytes> nativeInventoryPackets(const ClassicData &,const PersistentCharacter &,const hosting::NativeIdentityView &);
std::vector<Bytes> nativeInventoryDelta(const ClassicData &,const server::InventoryFact &,const hosting::NativeIdentityView &);
std::vector<Bytes> nativeTradeItems(const ClassicData &,const server::TradeItemsFact &,EntityId,const hosting::NativeIdentityView &);
}

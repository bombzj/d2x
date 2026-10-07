#pragma once
#include "gameplay/character/persistent_character.hpp"
#include "core/bytes.hpp"
#include <vector>
namespace d2x {
struct ClassicData;
// Server serialization of original 0x9D packets; never exposes D2S bytes to UI.
std::vector<Bytes> nativeInventoryPackets(const ClassicData &, const PersistentCharacter &);
}

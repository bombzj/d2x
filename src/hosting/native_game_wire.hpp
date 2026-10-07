#pragma once
#include "game_content.hpp"
#include "network/protocol/wire.hpp"
namespace d2x {
// Original 1.13c wire encoding at the hosting boundary, outside the authority.
std::vector<Bytes> nativeGameAdmission(const ClassicData &, const PersistentCharacter &, const GeneratedArea &, const PlayerSnapshot &);
Bytes nativePlayerMotion(const PlayerSnapshot &, Vec origin);
}

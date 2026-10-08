#pragma once
#include "game_content.hpp"
#include "network/protocol/wire.hpp"
namespace d2x {
// Original 1.13c wire encoding at the hosting boundary, outside the authority.
std::vector<Bytes> nativeGameAdmission(const ClassicData &, const PersistentCharacter &, const GeneratedArea &, const PlayerSnapshot &, const server::AreaMetadata &);
std::vector<Bytes> nativeAreaUnits(const GeneratedArea &, const server::AreaMetadata &);
Bytes nativePlayerAssignment(const ClassicData &, const PersistentCharacter &, const PlayerSnapshot &, Vec);
Bytes nativePlayerRoster(const ClassicData &, const CharacterRecord &);
Bytes nativePlayerMotion(const PlayerSnapshot &, Vec origin);
}

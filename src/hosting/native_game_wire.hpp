#pragma once
#include "game_content.hpp"
#include "network/protocol/wire.hpp"
namespace d2x {
// Original 1.13c wire encoding at the hosting boundary, outside the authority.
std::vector<Bytes> nativeGameAdmission(const ClassicData &, const PersistentCharacter &, const GeneratedArea &, const PlayerSnapshot &, const server::AreaMetadata &, const server::QuestFact &);
std::vector<Bytes> nativeAreaWarps(const GeneratedArea &, const server::AreaMetadata &);
Bytes nativePlayerAssignment(const ClassicData &, const PersistentCharacter &, const PlayerSnapshot &, Vec);
Bytes nativePlayerInitialStates(const ClassicData &, const PlayerSnapshot &);
Bytes nativePlayerRoster(const ClassicData &, const CharacterRecord &);
Bytes nativePlayerMotion(const PlayerSnapshot &, Vec origin, bool targetVisible=false);
struct NativePlayerResources {
    uint16_t life{},mana{},stamina{},x{},y{};
    uint8_t potionLife{},potionMana{},offsetX{},offsetY{};
    auto operator<=>(const NativePlayerResources &) const = default;
};
NativePlayerResources nativePlayerResources(const PlayerSnapshot &,Vec origin);
Bytes nativeResourceUpdate(const NativePlayerResources &,uint8_t message);
}

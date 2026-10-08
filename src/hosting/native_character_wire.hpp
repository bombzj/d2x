#pragma once
#include "network/protocol/wire.hpp"
#include "server/runtime/events.hpp"
namespace d2x {
struct ClassicData;
std::vector<Bytes> nativeMana(const ClassicData &, const server::ManaFact &);
std::vector<Bytes> nativeLife(const ClassicData &, const server::LifeFact &);
std::vector<Bytes> nativeCharacterPackets(const ClassicData &, const CharacterRecord &, const server::attributes::Totals &);
std::vector<Bytes> nativeCharacterDelta(const ClassicData &, const server::CharacterFact &);
}

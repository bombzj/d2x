#pragma once
#include "network/protocol/wire.hpp"
#include <vector>
namespace d2x {
struct ClassicData;
struct CharacterRecord;
namespace server {
struct CharacterFact;
namespace attributes { struct Totals; }
}
std::vector<Bytes> nativeCharacterPackets(const ClassicData &, const CharacterRecord &, const server::attributes::Totals &);
std::vector<Bytes> nativePublicCharacterAttributes(const ClassicData &, const CharacterRecord &);
std::vector<Bytes> nativeCharacterDelta(const ClassicData &, const server::CharacterFact &);
}

#pragma once
#include "network/protocol/wire.hpp"
#include <vector>
namespace d2x {
struct ClassicData;
struct CharacterRecord;
namespace server {
struct ManaFact;
struct LifeFact;
struct ResourcesFact;
struct CharacterFact;
namespace attributes { struct Totals; }
}
std::vector<Bytes> nativeMana(const ClassicData &, const server::ManaFact &);
std::vector<Bytes> nativeLife(const ClassicData &, const server::LifeFact &);
std::vector<Bytes> nativeResources(const ClassicData &, const server::ResourcesFact &);
std::vector<Bytes> nativeCharacterPackets(const ClassicData &, const CharacterRecord &, const server::attributes::Totals &);
std::vector<Bytes> nativePublicCharacterAttributes(const ClassicData &, const CharacterRecord &);
std::vector<Bytes> nativeCharacterDelta(const ClassicData &, const server::CharacterFact &);
}

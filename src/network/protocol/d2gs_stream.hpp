#pragma once
#include "network/protocol/wire.hpp"
#include <cstddef>
#include <span>

namespace d2x::net::protocol {
// One LoD 1.13c server stream. Negotiation is raw; later envelopes carry Huffman data.
// next() returns complete logical packets including those sharing one envelope.
class D2gsStream {
  public:
    void append(std::span<const uint8_t> bytes);
    bool next(Packet &packet);
    void reset();
    bool empty() const { return wire_.empty() && logical_.empty(); }
  private:
    enum class Mode { Negotiation, Raw, Huffman } mode_{Mode::Negotiation};
    Bytes wire_, logical_;
};
Bytes decompress_huffman(std::span<const uint8_t> bytes, size_t limit = 65536);
// Full packet size, including ID. Zero means incomplete; unsupported IDs throw.
size_t lod113c_packet_size(std::span<const uint8_t> bytes);
Bytes game_logon(uint32_t hash, uint16_t token, uint8_t characterClass,
    uint8_t locale, std::string_view characterName);
Bytes game_ping(uint32_t elapsedMilliseconds, uint32_t latency);
} // namespace d2x::net::protocol

#pragma once
#include "network/protocol/wire.hpp"
#include "network/protocol/client_profile.hpp"
#include <cstddef>
#include <span>

namespace d2x::net::protocol {
// One build's server stream. Negotiation is raw; later envelopes carry Huffman data.
// next() returns complete logical packets including those sharing one envelope.
class D2gsStream {
  public:
    explicit D2gsStream(ClientProfile profile = ClientProfile::Lod113c)
        : compatibility_(clientCompatibility(profile)) {}
    void append(std::span<const uint8_t> bytes);
    bool next(Packet &packet);
    void reset();
    bool empty() const { return wire_.empty() && logical_.empty(); }
  private:
    const ClientCompatibility &compatibility_;
    enum class Mode { Negotiation, Raw, Huffman } mode_{Mode::Negotiation};
    Bytes wire_, logical_;
};
Bytes decompress_huffman(std::span<const uint8_t> bytes, size_t limit = 65536);
// Full packet size, including ID. Zero means incomplete; unsupported IDs throw.
size_t lod113c_packet_size(std::span<const uint8_t> bytes);
Bytes game_logon(uint32_t hash, uint16_t token, uint8_t characterClass,
    uint8_t locale, std::string_view characterName);
Bytes game_ping(uint32_t elapsedMilliseconds, uint32_t latency);
// Fixed CN BH subcommand 0: cosmetic auras and pd2_like disabled. The loot
// preference is the same local setting used by CN room creation.
Bytes game_options_pd2_cn(bool lootAllocation);
Bytes game_tick_pd2_cn(uint32_t tick);
Bytes game_position_pd2_cn(uint16_t x, uint16_t y);
Bytes game_bh_report_pd2_cn(uint32_t fileCrc32);
enum class AutomapReportPd2Cn : uint32_t { Initial = 2, ExcessiveCalls = 0 };
Bytes automap_report_pd2_cn(AutomapReportPd2Cn);
// Broadcast/whisper/overhead with already encoded native ANSI payload. D2Net requires an
// additional zero extension-length byte after the two terminated strings.
Bytes game_chat(std::string_view text, std::string_view receiver = {}, bool overhead = false,uint8_t language=0);
} // namespace d2x::net::protocol

#include "network/protocol/client_profile.hpp"
#include "network/protocol/d2gs_stream.hpp"
#include "network/protocol/message_schema.hpp"
#include <utility>

namespace d2x::net::protocol {
namespace {
size_t cn_server_packet_size(std::span<const uint8_t>);
size_t pd2_local_server_packet_size(std::span<const uint8_t> bytes) {
    if(!bytes.empty() && bytes.front()==0x55) return 210;
    return lod113c_packet_size(bytes);
}
void validate_cn_client_packet(std::span<const uint8_t>);
constexpr ClientCompatibility original{
    ClientProfile::Lod113c, "lod113c", AccountPasswordCase::Preserve,
    RealmPasswordSource::AccountLogonProof, 1024, 0, 0, 0, {}, {},
    lod113c_packet_size, validateClientPacket};
// D2X hosting uses the existing MCP/D2GS envelopes and TCP/IP save transfer.
// PD2 resource rules and item payloads are independent of CN service extensions.
constexpr ClientCompatibility pd2Local{
    ClientProfile::Pd2Local, "pd2-local", AccountPasswordCase::Preserve,
    RealmPasswordSource::AccountLogonProof, 1024, 0, 0, 0, {}, {},
    pd2_local_server_packet_size, validateClientPacket};
// Core RVA 0x393E00 inserts GS and loot bytes before MCP 03 room strings.
constexpr ClientCompatibility cnNormal{
    ClientProfile::Pd2CnNormal20261020, "pd2-cn-normal-20261020", AccountPasswordCase::LowerAscii,
    RealmPasswordSource::FixedRealmPassword, 8, 2, 2, 9,
    {}, {},
    cn_server_packet_size, validate_cn_client_packet};

// ProjectDiablo.dll RVA 0x368600; full target identity and limits are recorded in
// docs/resources/PD2_CN_DLL_ANALYSIS.md. File identity is not inferred here.
// These are logical packet lengths including ID, not compression envelopes.
constexpr std::pair<uint8_t, size_t> cnServerLengths[]{
    {0x0A, 6}, {0x22, 12}, {0x2D, 5}, {0x2E, 18}, {0x2F, 11}, {0x30, 6},
    {0x31, 411}, {0x32, 10}, {0x33, 407}, {0x35, 411}, {0x36, 411},
    {0x55, 210}, {0x56, 66}, {0x66, 5}, {0x83, 16}, {0x84, 4}, {0x85, 393},
    {0x86, 7}, {0x87, 4}, {0x88, 387}, {0x97, 1}, {0xA5, 8}, {0xA9, 7}, {0xAD, 6}};
constexpr std::pair<uint8_t, size_t> cnClientLengths[]{
    {0x22, 11}, {0x2B, 5}, {0x2C, 3}, {0x2D, 8}, {0x2E, 5}, {0x39, 12}, {0x3A, 3},
    {0x3B, 4}, {0x42, 7}, {0x43, 10}, {0x45, 258}, {0x4A, 260}, {0x4C, 11},
    {0x4E, 4}, {0x52, 22}, {0x55, 2}, {0x56, 131}, {0x57, 5},
    {0x5A, 1}, {0x5B, 5}, {0x5C, 13}};
size_t cn_server_packet_size(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return 0;
    for (const auto &[id, length] : cnServerLengths)
        if (bytes[0] == id) return length;
    // 9C and AC retain their original variable length offsets; their payload
    // changes belong to the build's consumers, not to this stream framer.
    return lod113c_packet_size(bytes);
}
void validate_cn_client_packet(std::span<const uint8_t> bytes) {
    if (!bytes.empty())
        for (const auto &[id, length] : cnClientLengths)
            if (bytes[0] == id) {
                if (bytes.size() != length) throw ProtocolError("Invalid PD2 CN client packet size");
                return;
            }
    validateClientPacket(bytes);
}
}
const ClientCompatibility &clientCompatibility(ClientProfile profile) {
    switch (profile) {
    case ClientProfile::Lod113c: return original;
    case ClientProfile::Pd2CnNormal20261020: return cnNormal;
    case ClientProfile::Pd2Local: return pd2Local;
    }
    throw ProtocolError("Unknown client protocol profile");
}
} // namespace d2x::net::protocol

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace d2x::net::protocol {
// An explicitly selected wire dialect. Resources and authentication are read
// from the selected files; packet and format consumers enforce their contracts.
enum class ClientProfile { Lod113c, Pd2CnNormal20261020, Pd2Local };
enum class Pd2CoreBuild { Normal20261020, Normal20261021, Normal20261026 };
enum class AccountPasswordCase { Preserve, LowerAscii };
enum class RealmPasswordSource { AccountLogonProof, FixedRealmPassword };
struct ClientCompatibility {
    ClientProfile profile;
    std::string_view id;
    AccountPasswordCase accountPasswordCase;
    RealmPasswordSource realmPasswordSource;
    uint32_t characterListRequest;
    size_t gameListExtensionBytes;
    size_t gameCreationExtensionBytes; // Native GS selection and loot mode before the room strings.
    uint8_t maximumGameServer; // Zero for profiles without a native server selector.
    // Admission request gates. Each ordered game consumer independently rejects
    // unsupported payloads; an empty gate does not certify full gameplay.
    std::string_view authenticationBlocker, gameEntryBlocker;
    size_t (*serverPacketSize)(std::span<const uint8_t>);
    void (*validateClientPacket)(std::span<const uint8_t>);
};
const ClientCompatibility &clientCompatibility(ClientProfile);
} // namespace d2x::net::protocol

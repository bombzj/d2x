#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace d2x::net::protocol {
// A fixed source build's dialect. The application must bind and verify its
// resource/authentication files before exposing it as a playable profile.
enum class ClientProfile { Lod113c, Pd2CnNormal20261020 };
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

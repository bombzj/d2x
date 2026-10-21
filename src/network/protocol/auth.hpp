#pragma once
#include "core/bytes.hpp"
#include "network/protocol/client_profile.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace d2x::net::protocol {
using Digest = std::array<uint8_t, 20>;
struct AuthChallenge {
    uint32_t logonType{}, serverToken{}, udpToken{};
    uint64_t fileTime{};
    std::string revisionFile, formula;
};
struct OriginalClientAuth {
    // Game.exe, Bnclient.dll, D2Client.dll, in CheckRevision order. Read-only.
    std::array<std::filesystem::path, 3> files;
    bool submitKeys{}; // PvPGN accepts a zero-key SID_AUTH_CHECK.
    std::string classicKey, expansionKey, owner;
    ClientProfile profile{ClientProfile::Lod113c};
    bool nativeMpqKeys{}; // CN proof comparison/count semantics require this source.
};
struct KeyProof {
    uint32_t length{}, product{}, publicValue{};
    Digest hash{};
};
struct AuthProof {
    uint32_t executableVersion{}, executableHash{};
    std::vector<KeyProof> keys;
    std::string executableInfo, owner;
    std::array<std::string,2> cnKeyHashes;
    std::array<uint16_t,3> cnKeyMasks{};
};
// No placeholder keys or version hashes. Computes against supplied original files.
AuthProof prepare_auth(const AuthChallenge &, uint32_t clientToken, const OriginalClientAuth &);
Bytes encode_auth_check(ClientProfile, uint32_t clientToken, AuthProof &);
void erase_auth_secrets(OriginalClientAuth &);
Digest password_hash(std::string_view password);
Digest account_password_hash(ClientProfile, std::string_view password);
Digest logon_hash(uint32_t clientToken, uint32_t serverToken, const Digest &passwordHash);
Digest realm_logon_hash(ClientProfile, uint32_t clientToken, uint32_t serverToken,
                       const Digest &accountLogonProof);
void erase_secret(std::string &value);
void erase_secret(Digest &value);
void erase_secret(Bytes &value);
} // namespace d2x::net::protocol

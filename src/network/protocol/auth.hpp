#pragma once
#include "core/bytes.hpp"
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
};
struct KeyProof {
    uint32_t length{}, product{}, publicValue{};
    Digest hash{};
};
struct AuthProof {
    uint32_t executableVersion{}, executableHash{};
    std::vector<KeyProof> keys;
    std::string executableInfo, owner;
};
// No placeholder keys or version hashes. Computes against supplied original files.
AuthProof prepare_auth(const AuthChallenge &, uint32_t clientToken, const OriginalClientAuth &);
Digest password_hash(std::string_view password);
Digest logon_hash(uint32_t clientToken, uint32_t serverToken, const Digest &passwordHash);
void erase_secret(std::string &value);
void erase_secret(Digest &value);
void erase_secret(Bytes &value);
} // namespace d2x::net::protocol

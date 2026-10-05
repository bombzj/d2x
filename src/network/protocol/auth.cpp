#include "network/protocol/auth.hpp"
#include "network/protocol/wire.hpp"
#include <algorithm>
#include <array>
// BNCSutil's bsha1.h requires export macros from mutil.h first.
// clang-format off
#include <bncsutil/mutil.h>
#include <bncsutil/bsha1.h>
#include <bncsutil/cdkeydecoder.h>
#include <bncsutil/checkrevision.h>
// clang-format on
#include <charconv>
#include <mutex>
#include <regex>
#include <vector>

namespace d2x::net::protocol {
namespace {
std::mutex authMutex; // BNCSutil's seed table is process-global.
void validate_formula(std::string_view formula) {
    // The legacy parser leaves absent initializers uninitialized and has four
    // operation slots. Validate every token before letting it read server input.
    if (formula.empty() || formula.size() > 256)
        throw ProtocolError("Invalid CheckRevision formula length");
    std::vector<std::string_view> tokens;
    size_t start{};
    for (;;) {
        const auto end = formula.find(' ', start);
        tokens.push_back(formula.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    if (tokens.size() < 5 || tokens.size() > 8)
        throw ProtocolError("Unsupported CheckRevision formula");
    std::array<bool, 3> initialized{};
    for (size_t i = 0; i < 3; ++i) {
        const auto token = tokens[i];
        if (token.size() < 3 || token.size() > 12 || token[0] < 'A' || token[0] > 'C' || token[1] != '=')
            throw ProtocolError("Invalid CheckRevision initializer");
        const size_t variable = size_t(token[0] - 'A');
        uint32_t value{};
        const auto result = std::from_chars(token.data() + 2, token.data() + token.size(), value);
        if (initialized[variable] || result.ec != std::errc{} || result.ptr != token.data() + token.size())
            throw ProtocolError("Invalid CheckRevision initializer");
        initialized[variable] = true;
    }
    if (tokens[3].size() != 1 || tokens[3][0] < '1' || tokens[3][0] > '4' ||
        size_t(tokens[3][0] - '0') != tokens.size() - 4)
        throw ProtocolError("Invalid CheckRevision operation count");
    const auto variable = [](char c) { return (c >= 'A' && c <= 'C') || c == 'S'; };
    for (size_t i = 4; i < tokens.size(); ++i) {
        const auto op = tokens[i];
        if (op.size() != 5 || op[0] < 'A' || op[0] > 'C' || op[1] != '=' || !variable(op[2]) ||
            !variable(op[4]) || std::string_view("+*-^").find(op[3]) == std::string_view::npos)
            throw ProtocolError("Unsupported CheckRevision operation");
    }
}
KeyProof key_proof(std::string_view key, uint32_t client, uint32_t server, bool expansion) {
    if ((key.size() != 16 && key.size() != 26) || key.find('\0') != std::string_view::npos)
        throw ProtocolError("A 16 or 26 character Diablo II key is required");
    CDKeyDecoder decoder(key.data(), key.size());
    if (!decoder.isKeyValid())
        throw ProtocolError("Invalid Diablo II key");
    KeyProof proof;
    proof.length = uint32_t(key.size());
    proof.product = decoder.getProduct();
    // 16-character products are 6/10; 26-character products are 24/25.
    const uint32_t expected = key.size() == 16 ? (expansion ? 10u : 6u) : (expansion ? 25u : 24u);
    if (proof.product != expected)
        throw ProtocolError("Diablo II key product does not match");
    proof.publicValue = decoder.getVal1();
    if (decoder.calculateHash(client, server) != proof.hash.size() ||
        decoder.getHash(reinterpret_cast<char *>(proof.hash.data())) != proof.hash.size())
        throw ProtocolError("Diablo II key proof failed");
    return proof;
}
} // namespace
AuthProof prepare_auth(const AuthChallenge &challenge, uint32_t token, const OriginalClientAuth &config) {
    if (challenge.logonType != 0)
        throw ProtocolError("Unsupported account authentication type");
    validate_formula(challenge.formula);
    static const std::regex revision(R"(^IX86ver[0-7]\.mpq$)");
    if (!std::regex_match(challenge.revisionFile, revision))
        throw ProtocolError("Unsupported CheckRevision module");
    std::array<std::string, 3> files;
    std::array<const char *, 3> pointers{};
    for (size_t i = 0; i < files.size(); ++i) {
        if (!std::filesystem::is_regular_file(config.files[i]) ||
            std::filesystem::file_size(config.files[i]) > 64 * 1024 * 1024)
            throw ProtocolError("Original client authentication file missing or too large");
        files[i] = config.files[i].string();
        if (std::any_of(files[i].begin(), files[i].end(), [](unsigned char c) { return c >= 128; }))
            throw ProtocolError("Legacy authentication requires ASCII file paths");
        pointers[i] = files[i].c_str();
    }
    const std::lock_guard lock(authMutex);
    unsigned long checksum{};
    const int module = extractMPQNumber(challenge.revisionFile.c_str());
    if (module < 0 || !checkRevision(challenge.formula.c_str(), pointers.data(), 3, module, &checksum))
        throw ProtocolError("CheckRevision calculation failed");
    AuthProof result;
    std::array<char, 1024> information{};
    const int length = getExeInfo(files[0].c_str(), information.data(), information.size(),
                                  &result.executableVersion, BNCSUTIL_PLATFORM_X86);
    if (length <= 0 || size_t(length) >= information.size())
        throw ProtocolError("Original executable version information unavailable");
    result.executableHash = uint32_t(checksum);
    result.executableInfo = information.data();
    result.owner = config.owner;
    if (config.submitKeys) {
        result.keys.push_back(key_proof(config.classicKey, token, challenge.serverToken, false));
        result.keys.push_back(key_proof(config.expansionKey, token, challenge.serverToken, true));
    }
    return result;
}
Digest password_hash(std::string_view password) {
    if (password.empty() || password.size() > 128 || password.find('\0') != std::string_view::npos)
        throw ProtocolError("Invalid account password length");
    Digest result{};
    calcHashBuf(password.data(), password.size(), reinterpret_cast<char *>(result.data()));
    return result;
}
Digest logon_hash(uint32_t clientToken, uint32_t serverToken, const Digest &passwordHash) {
    Writer in;
    in.u32(clientToken);
    in.u32(serverToken);
    in.append(passwordHash);
    auto bytes = in.release();
    Digest result{};
    calcHashBuf(reinterpret_cast<const char *>(bytes.data()), bytes.size(),
                reinterpret_cast<char *>(result.data()));
    volatile uint8_t *secret = bytes.data();
    for (size_t i = 0; i < bytes.size(); ++i)
        secret[i] = 0;
    return result;
}
void erase_secret(std::string &value) {
    volatile char *secret = value.data();
    for (size_t i = 0; i < value.size(); ++i)
        secret[i] = 0;
    value.clear();
}
void erase_secret(Digest &value) {
    volatile uint8_t *secret = value.data();
    for (size_t i = 0; i < value.size(); ++i)
        secret[i] = 0;
}
void erase_secret(Bytes &value) {
    volatile uint8_t *secret = value.data();
    for (size_t i = 0; i < value.size(); ++i)
        secret[i] = 0;
    value.clear();
}
} // namespace d2x::net::protocol

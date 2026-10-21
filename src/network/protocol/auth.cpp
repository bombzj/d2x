#include "network/protocol/auth.hpp"
#include "network/protocol/wire.hpp"
#include "core/sha256.hpp"
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
    const bool cn = config.profile == ClientProfile::Pd2CnNormal20261020;
    auto formula = challenge.formula;
    if (cn) formula.resize(formula.find(':') == std::string::npos ? formula.size() : formula.find(':'));
    validate_formula(formula);
    static const std::regex revision(R"(^IX86ver[0-7]\.mpq$)");
    if (cn ? challenge.revisionFile != "ver-IX86-1.mpq" : !std::regex_match(challenge.revisionFile, revision))
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
    const int module = cn ? 1 : extractMPQNumber(challenge.revisionFile.c_str());
    if (module < 0 || !checkRevision(formula.c_str(), pointers.data(), 3, module, &checksum))
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
    if (cn) {
        if (!config.submitKeys || !config.nativeMpqKeys || config.owner.empty() || config.owner.size()>64 ||
            config.owner.find('\0')!=std::string::npos)
            throw ProtocolError("PD2 CN requires native MPQ key and owner material");
        // ProjectDiablo 0x1E76E0 / 0x3943E0: character groups share a word,
        // each character has its own bit. No key or derived proof enters a view.
        constexpr std::array<std::string_view,3> groups{"30PATGH2XR7W","6NOUI18VBYMJ","45QDLKZES9CF"};
        constexpr std::array<std::array<uint16_t,12>,3> bits{{
            {1,4,0x10,0x20,0x40,0x80,0x100,0x200,0x800,0x2000,0x4000,0x8000},
            {2,4,0x10,0x20,0x40,0x100,0x200,0x800,0x1000,0x2000,0x4000,0x8000},
            {1,2,4,8,0x20,0x40,0x80,0x200,0x400,0x800,0x2000,0x4000}}};
        const std::array<std::string_view,2> keys{config.classicKey,config.expansionKey};
        for(size_t i=0;i<keys.size();++i) {
            const auto key=keys[i];
            if(key.size()!=26 || std::any_of(key.begin(),key.end(),[](unsigned char c){return c<32 || c>=127;}))
                throw ProtocolError("PD2 CN requires two 26-byte native keys");
            result.cnKeyHashes[i]=sha256Hex({reinterpret_cast<const uint8_t *>(key.data()),key.size()});
            for(const auto c:key) {
                bool known=false;
                for(size_t group=0;group<groups.size();++group)
                    if(const auto at=groups[group].find(c);at!=std::string_view::npos) {
                        result.cnKeyMasks[group]|=bits[group][at];known=true;break;
                    }
                if(!known) result.cnKeyMasks[2]|=0x8000;
            }
        }
    } else if (config.submitKeys) {
        result.keys.push_back(key_proof(config.classicKey, token, challenge.serverToken, false));
        result.keys.push_back(key_proof(config.expansionKey, token, challenge.serverToken, true));
    }
    return result;
}
Bytes encode_auth_check(ClientProfile profile, uint32_t token, AuthProof &proof) {
    struct Wipe { AuthProof &proof; ~Wipe(){
        for(auto &hash:proof.cnKeyHashes) erase_secret(hash);
        for(auto &key:proof.keys) erase_secret(key.hash);
        erase_secret(proof.owner);
    }} wipe{proof};
    Writer out;
    out.u32(token);out.u32(proof.executableVersion);out.u32(proof.executableHash);
    if(profile==ClientProfile::Pd2CnNormal20261020) {
        // MPQ key source: the native re-read compares equal to the active keys
        // and owner, so no extra proof pair/count increment is emitted.
        if(proof.cnKeyHashes[0].size()!=64 || proof.cnKeyHashes[1].size()!=64)
            throw ProtocolError("Missing PD2 CN key proof");
        out.u32(2);
        for(auto mask:proof.cnKeyMasks) out.u16(mask);
        out.u32(26);out.u32(26);
        for(const auto &hash:proof.cnKeyHashes) out.string(hash,64);
    } else {
        out.u32(uint32_t(proof.keys.size()));out.u32(0);
        for(const auto &key:proof.keys) {
            out.u32(key.length);out.u32(key.product);out.u32(key.publicValue);out.u32(0);out.append(key.hash);
        }
    }
    out.string(proof.executableInfo,1024);out.string(proof.owner,64);
    return out.release();
}
void erase_auth_secrets(OriginalClientAuth &auth) {
    erase_secret(auth.classicKey);erase_secret(auth.expansionKey);erase_secret(auth.owner);
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
Digest account_password_hash(ClientProfile profile, std::string_view password) {
    if (clientCompatibility(profile).accountPasswordCase == AccountPasswordCase::Preserve)
        return password_hash(password);
    if (password.empty() || password.size() > 128 ||
        std::any_of(password.begin(), password.end(), [](unsigned char c) { return c < 32 || c >= 127; }))
        throw ProtocolError("PD2 CN account passwords currently require printable ASCII");
    std::string normalized(password);
    for (auto &c : normalized) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    const auto result = password_hash(normalized);
    erase_secret(normalized);
    return result;
}
Digest realm_logon_hash(ClientProfile profile, uint32_t clientToken, uint32_t serverToken,
                       const Digest &accountLogonProof) {
    if (clientCompatibility(profile).realmPasswordSource == RealmPasswordSource::AccountLogonProof)
        return accountLogonProof; // Preserve the existing PvPGN account contract.
    auto fixedPasswordHash = password_hash("password");
    const auto result = logon_hash(clientToken, serverToken, fixedPasswordHash);
    erase_secret(fixedPasswordHash);
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

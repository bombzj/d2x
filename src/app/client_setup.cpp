#include "app/client_setup.hpp"
#include "app/options.hpp"
#include "core/sha256.hpp"
#include "network/protocol/auth.hpp"
#include "network/protocol/native_auth_resource.hpp"
#include "resources/archive.hpp"
#include "resources/pd2/archives.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>

namespace d2x {
namespace {
std::string fileDigest(const std::filesystem::path &path, uint32_t *crc32 = nullptr) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Required client file is unavailable: " + path.filename().string());
    Sha256 hash;
    uint32_t crc = 0xFFFFFFFFu;
    std::array<uint8_t, 65536> block{};
    while (input) {
        input.read(reinterpret_cast<char *>(block.data()), block.size());
        hash.append(std::span<const uint8_t>(block).first(size_t(input.gcount())));
        if (crc32) for (const auto byte : std::span<const uint8_t>(block).first(size_t(input.gcount()))) {
            crc ^= byte;
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    if (!input.eof()) throw std::runtime_error("Cannot read client file: " + path.filename().string());
    if (crc32) *crc32 = crc ^ 0xFFFFFFFFu;
    return hexDigest(hash.finish());
}
std::filesystem::path profilePath(const nlohmann::json &json, const char *key,
                                  const std::filesystem::path &base) {
    const auto value = std::filesystem::path(json.at(key).get<std::string>());
    if (value.empty()) throw std::runtime_error(std::string("Empty client profile path: ") + key);
    return std::filesystem::weakly_canonical(value.is_absolute() ? value : base/value);
}
}
void verifyClientProfileDefinition(const std::filesystem::path &path, std::string_view digest) {
    if(digest.empty() || fileDigest(path)!=digest)
        throw std::runtime_error("Client profile changed; restart D2X to select the new service/resources");
}
void preparePd2Authentication(net::protocol::OriginalClientAuth &auth, const std::filesystem::path &pd2,
                               const std::filesystem::path &original) {
    using namespace net::protocol;
    // Explicit MPQ source; do not silently substitute loose .gid/.clh keys.
    // The resource re-read in ProjectDiablo 0x393F80 then compares equal and
    // emits no additional proof pair. The actual files remain read-only.
    auto decode = [](Archives &archive, const char *member) {
        auto bytes = archive.read(member);
        return decode_native_auth_resource(bytes);
    };
    try {
        // This identity selects optional core-counter metadata only. An unknown
        // core must not guess another layout or block unrelated account login.
        auth.cnCoreBuild.reset();
        const auto coreDigest = fileDigest(pd2/"ProjectDiablo.dll");
        if (coreDigest == "9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f")
            auth.cnCoreBuild = Pd2CoreBuild::Normal20261020;
        else if (coreDigest == "17d5fb1b049d8076226d27295487b49c8b5f2629553c193a01aa1e3d8afaba57")
            auth.cnCoreBuild = Pd2CoreBuild::Normal20261021;
        uint32_t bhCrc{};
        // Native BH reports its actual disk bytes. Keep reporting the selected
        // file after updates rather than requiring an old sample's SHA-256.
        fileDigest(pd2/"BH.dll", &bhCrc);
        auth.cnBhCrc32 = bhCrc;
        constexpr auto classicMember = "data/global/sfx/cursor/wavindx.wav";
        Archives sound;
        if(std::filesystem::is_regular_file(original/"d2sfx.mpq")) sound.mount(original/"d2sfx.mpq");
        if(!sound.contains(classicMember)) sound.mount(original/"d2data.mpq");
        auth.classicKey=decode(sound,classicMember);
        Archives characters;characters.mount(original/"d2char.mpq");
        auth.expansionKey=decode(characters,"data/global/chars/am/cof/amblxbow.cof");
        auth.owner=decode(characters,"data/global/sfx/cursor/curindx.wav");
        if(auth.classicKey.size()!=26 || auth.expansionKey.size()!=26 || auth.owner.empty() || auth.owner.size()>64)
            throw std::runtime_error("PD2 CN requires the original 26-byte MPQ key resources and owner");
        constexpr std::array names{"Game.exe","Bnclient.dll","D2Client.dll"};
        for(size_t i=0;i<names.size();++i) {
            auth.files[i]=pd2/names[i];
            if(!std::filesystem::is_regular_file(auth.files[i]) ||
               std::filesystem::file_size(auth.files[i])>64u*1024*1024)
                throw std::runtime_error("PD2 CN authentication file missing or too large: "+std::string(names[i]));
        }
        auth.profile=ClientProfile::Pd2CnNormal20261020;
        auth.submitKeys=true;
        auth.nativeMpqKeys=true;
    } catch (...) { erase_auth_secrets(auth);throw; }
}
void resolveClientSetup(AppOptions &options) {
    if (options.clientProfile.empty()) return;
    if(!options.pack.empty()) throw std::runtime_error("PD2 resource packing is unavailable");
    options.clientProfileDigest=fileDigest(options.clientProfile);
    std::ifstream input(options.clientProfile);
    if (!input) throw std::runtime_error("Client profile is unavailable");
    const auto json = nlohmann::json::parse(input);
    verifyClientProfileDefinition(options.clientProfile,options.clientProfileDigest);
    if(json.value("version",0)==1 && json.value("clientProfile",std::string{})=="pd2-local") {
        if(json.value("authentication",std::string("pvpgn"))!="pvpgn")
            throw std::runtime_error("The self-hosted PD2 realm uses PvPGN authentication");
        const auto base=std::filesystem::absolute(options.clientProfile).parent_path();
        options.mpq=profilePath(json,"originalClientDirectory",base).string();
        options.pd2Directory=profilePath(json,"pd2Directory",base).string();
        options.mpqSupplement.clear();
        options.protocolProfile=net::protocol::ClientProfile::Pd2Local;
        options.onlineConfig=std::filesystem::absolute(options.clientProfile).string();
        if(options.language.empty()) options.language="chi";
        return;
    }
    if(!options.headlessProfile.empty())
        throw std::runtime_error("Use a pd2-local resource profile for self-hosted headless PD2");
    if (json.value("version", 0) != 1 ||
        json.value("clientProfile", std::string{}) != "pd2-cn-normal-20261020" ||
        json.value("authentication", std::string{}) != "pd2-cn")
        throw std::runtime_error("Unsupported client profile definition");
    const auto base = std::filesystem::absolute(options.clientProfile).parent_path();
    const auto pd2 = profilePath(json, "pd2Directory", base);
    const auto original = profilePath(json, "originalClientDirectory", base);
    if (!std::filesystem::is_directory(pd2) || !std::filesystem::is_directory(original) ||
        !std::filesystem::equivalent(pd2.parent_path(), original))
        throw std::runtime_error("PD2 CN requires a pd2 directory inside the isolated original client copy");
    options.protocolProfile = net::protocol::ClientProfile::Pd2CnNormal20261020;
    options.mpq = original.string();
    options.pd2Directory = pd2.string();
    options.mpqSupplement.clear();
    // The CN client ships Data/local/Lng/Chi, Font/Chi and UI/Chi; without an
    // explicit override a PD2 profile reads Chinese tables and interface art.
    if (options.language.empty()) options.language = "chi";
    options.onlineConfig = std::filesystem::absolute(options.clientProfile).string();
}
void mountClientResources(Archives &archives, const AppOptions &options) {
    if(options.protocolProfile!=net::protocol::ClientProfile::Lod113c) {
        mountPd2HostArchives(archives,options.mpq,options.pd2Directory);
        return;
    }
    std::error_code error;
    if (!options.mpqSupplement.empty() &&
        !std::filesystem::equivalent(options.mpqSupplement, options.mpq, error))
        archives.mountDirectory(options.mpqSupplement);
    archives.mountDirectory(options.mpq);
}
} // namespace d2x

#include "app/client_setup.hpp"
#include "app/options.hpp"
#include "core/sha256.hpp"
#include "network/protocol/auth.hpp"
#include "network/protocol/native_auth_resource.hpp"
#include "resources/archive.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace d2x {
namespace {
constexpr std::pair<std::string_view, std::string_view> cnFiles[]{
    {"ProjectDiablo.dll","9c2b4d4627112d7f14ab959e71c56e947b7ceeb6f16424a637364f614d39122f"},
    {"Game.exe","065aedaa998adcd337b93ccf33457e3bac589bab3bfd894351b6fa834399985d"},
    {"Bnclient.dll","3631ca889dfa3cb149dd5b1722d3231851661b40514d4fbb4f00c529e76a56f5"},
    {"D2Client.dll","dd8bc6025de921216a97c17f97cd1a50fbb85926e838ec60e13451448836d906"},
    {"ver-IX86-1.mpq","b5deb6ffeb88de8f58942b67e75b0f28aaa6703947cc968e1a0a21d378d18fef"},
    {"patch_d2.mpq","b976e7847dda696f54198a9477819f96579c4894f54381c72afb4937e919e0a9"},
    {"pd2data.mpq","6ce1d885dfcb16ac0ff7a183d56c56baeaad591d8779ce9e843e16dfc2756002"},
    {"pd2assets.mpq","70baa8e5334b38dd2c4e3cb8f6517eab0a1cc46e49f667e111af687f764b7ac7"},
    {"pd2maps.mpq","ad704b94aab4fa4142fb4fe37b38bd3fe7ae43a94c14518baf0d6cb0ca00d33a"},
    {"pd2monchars.mpq","ae2abade85c5f393db5d8089cb4b6566b2cfed2adea0f573bc0f4220ef733217"}};
std::string fileDigest(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Required client file is unavailable: " + path.filename().string());
    Sha256 hash;
    std::array<uint8_t, 65536> block{};
    while (input) {
        input.read(reinterpret_cast<char *>(block.data()), block.size());
        hash.append(std::span<const uint8_t>(block).first(size_t(input.gcount())));
    }
    if (!input.eof()) throw std::runtime_error("Cannot read client file: " + path.filename().string());
    return hexDigest(hash.finish());
}
std::filesystem::path profilePath(const nlohmann::json &json, const char *key,
                                  const std::filesystem::path &base) {
    const auto value = std::filesystem::path(json.at(key).get<std::string>());
    if (value.empty()) throw std::runtime_error(std::string("Empty client profile path: ") + key);
    return std::filesystem::weakly_canonical(value.is_absolute() ? value : base/value);
}
}
void verifyPd2ClientFiles(const std::filesystem::path &root) {
    for (const auto &[name, expected] : cnFiles)
        if (fileDigest(root/std::string(name)) != expected)
            throw std::runtime_error("Unsupported PD2 CN build or changed client file: " + std::string(name));
}
void verifyPd2AuthenticationFiles(const std::filesystem::path &root) {
    // The first five registered files bind the native auth implementation and
    // CheckRevision inputs. Resource MPQs were verified before their handles
    // and frontend assets were created; do not hash them again on every login.
    for(const auto &[name,expected]:std::span(cnFiles).first(5))
        if(fileDigest(root/std::string(name))!=expected)
            throw std::runtime_error("PD2 CN authentication file changed; restart after restoring the selected build");
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
        for(size_t i=0;i<names.size();++i) auth.files[i]=pd2/names[i];
        auth.profile=ClientProfile::Pd2CnNormal20261020;
        auth.submitKeys=true;
        auth.nativeMpqKeys=true;
    } catch (...) { erase_auth_secrets(auth);throw; }
}
void resolveClientSetup(AppOptions &options) {
    if (options.clientProfile.empty()) return;
    if (!options.headlessProfile.empty() || !options.hostLan.empty() || !options.lan.empty() ||
        !options.load.empty() || !options.save.empty() || !options.characterClass.empty() ||
        !options.onlinePlay.empty() || !options.onlineCreateGame.empty() || !options.onlineJoinGame.empty() ||
        !options.hostPacketLog.empty() || !options.gamePacketLog.empty() || !options.pack.empty())
        throw std::runtime_error("PD2 CN currently supports the account/character lobby only");
    options.clientProfileDigest=fileDigest(options.clientProfile);
    std::ifstream input(options.clientProfile);
    if (!input) throw std::runtime_error("Client profile is unavailable");
    const auto json = nlohmann::json::parse(input);
    verifyClientProfileDefinition(options.clientProfile,options.clientProfileDigest);
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
    verifyPd2ClientFiles(pd2);
    options.protocolProfile = net::protocol::ClientProfile::Pd2CnNormal20261020;
    options.mpq = original.string();
    options.pd2Directory = pd2.string();
    options.mpqSupplement.clear();
    options.onlineConfig = std::filesystem::absolute(options.clientProfile).string();
}
void mountClientResources(Archives &archives, const AppOptions &options) {
    if (options.protocolProfile == net::protocol::ClientProfile::Lod113c) {
        std::error_code error;
        if (!options.mpqSupplement.empty() &&
            !std::filesystem::equivalent(options.mpqSupplement, options.mpq, error))
            archives.mountDirectory(options.mpqSupplement);
        archives.mountDirectory(options.mpq);
        return;
    }
    archives.mountDirectory(options.mpq);
    const auto pd2 = std::filesystem::path(options.pd2Directory);
    // Reverse search: monchars > maps > assets > data > CN patch > original.
    for (const auto *name : {"patch_d2.mpq", "pd2data.mpq", "pd2assets.mpq", "pd2maps.mpq", "pd2monchars.mpq"})
        archives.mount(pd2/name);
    archives.gameDirectory = pd2;
}
} // namespace d2x

#include "archives.hpp"
#include "resources/archive.hpp"
#include "core/sha256.hpp"
#include <array>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace d2x {
namespace {
std::string archiveDigest(const std::filesystem::path &path) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Required PD2 archive is unavailable: "+path.filename().string());
    Sha256 hash;
    std::array<uint8_t,65536> block{};
    while(input) {
        input.read(reinterpret_cast<char *>(block.data()),block.size());
        hash.append(std::span<const uint8_t>(block).first(size_t(input.gcount())));
    }
    if(!input.eof()) throw std::runtime_error("Cannot read PD2 archive: "+path.filename().string());
    return hexDigest(hash.finish());
}
}
void mountPd2HostArchives(Archives &archives,const std::filesystem::path &original,
                          const std::filesystem::path &pd2) {
    // Fixed current resource identity from docs/pd2/BUILDS.md. These are file
    // identities, not gameplay parameters; all content is read from the MPQs.
    constexpr std::pair<std::string_view,std::string_view> files[]{
        {"patch_d2.mpq","b976e7847dda696f54198a9477819f96579c4894f54381c72afb4937e919e0a9"},
        {"pd2data.mpq","cc42f5c71d61c9a38d5b948243ed2fe6a9a2bc0124fb3eaaff0f1943830e15cf"},
        {"pd2assets.mpq","2ceb83ca8e9f482fa46a94cda95d2ab80f8415a0d2c1047a44f64d4a95cf56af"},
        {"pd2maps.mpq","fb9e21ae5d797caa7ae8051ed15752a71a35ac317b44c8cdde3c443ad62b25eb"},
        {"pd2monchars.mpq","ae2abade85c5f393db5d8089cb4b6566b2cfed2adea0f573bc0f4220ef733217"}};
    for(const auto &[name,expected]:files)
        if(archiveDigest(pd2/std::string(name))!=expected)
            throw std::runtime_error("Unsupported PD2 resource build: "+std::string(name));
    archives.mountDirectory(original);
    if(archives.names.empty()) throw std::runtime_error("No original MPQ content mounted");
    for(const auto &[name,expected]:files) { (void)expected;archives.mount(pd2/std::string(name)); }
    archives.gameDirectory=pd2;
    if(std::filesystem::is_directory(pd2/"Data"/"local"))
        archives.mountLooseDirectory(pd2/"Data"/"local","data/local");
}
}

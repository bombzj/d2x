#include "archives.hpp"
#include "resources/archive.hpp"
#include <stdexcept>

namespace d2x {
void mountPd2HostArchives(Archives &archives,const std::filesystem::path &original,
                          const std::filesystem::path &pd2) {
    // MPQ updates are data inputs, not native authentication implementations.
    // Mount validates each archive; consumers validate the structures they use.
    // Evidence hashes in BUILDS.md must not become a resource whitelist.
    constexpr const char *files[]{"patch_d2.mpq","pd2data.mpq","pd2assets.mpq","pd2maps.mpq","pd2monchars.mpq"};
    archives.mountDirectory(original);
    if(archives.names.empty()) throw std::runtime_error("No original MPQ content mounted");
    for(const auto *name:files) archives.mount(pd2/name);
    archives.pd2Presets=true;
    archives.gameDirectory=pd2;
    if(std::filesystem::is_directory(pd2/"Data"/"local"))
        archives.mountLooseDirectory(pd2/"Data"/"local","data/local");
}
}

#pragma once
#include "core/bytes.hpp"
#include "path.hpp"
#include "core/fingerprint.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <map>
#include <memory>
#include <typeindex>
namespace d2x {
Bytes readFile(const std::filesystem::path &path);
void writeFile(const std::filesystem::path &path, std::span<const uint8_t> data);
class Archives {
    std::vector<void *> handles;
    std::vector<std::filesystem::path> mountedPaths_;
    std::function<void()> loadingPulse;
    mutable bool listed_{};
    // Owned by this mounted resource set, not by a player, room or connection.
    mutable std::map<std::type_index, std::shared_ptr<const void>> prepared_;
    mutable std::map<std::string, Bytes, std::less<>> readCache_;
    mutable size_t cacheBytes_{};
    // Loose members addressed as <prefix>/<path relative to root>, normalized.
    // They win over mounted archives, so a client's own Data folder supplies its
    // language tables, fonts and interface art as shipped.
    std::map<std::string, std::filesystem::path, std::less<>> loose_;
    Fingerprint mountIdentity_;
    void retain(const std::string &name, const Bytes &value) const;

  public:
    std::vector<std::string> names;
    mutable std::set<std::string> used;
    bool pd2Presets{}; // Selected by the frozen PD2 resource mount, before preparing rooms.
    // Selected language resource set (shared by every consumer of this mount).
    // Primitive fields keep resources independent of content definitions.
    std::string languageDirectory{"eng"};
    std::string languageFontDirectory{"latin"};
    // Root supplied through --mpq; used to locate loose game assets such as
    // optional outline fonts under <gameDirectory>/fonts.
    std::filesystem::path gameDirectory;
    uint32_t languageLocaleId{1033};
    uint32_t languageCode{0x656E5553u};
    uint8_t languageGameLocale{0};
    Archives() = default;
    Archives(const Archives &) = delete;
    Archives &operator=(const Archives &) = delete;
    ~Archives();
    std::unique_ptr<Archives> independent() const;
    void mount(const std::filesystem::path &path);
    uint64_t mountIdentity() const { return mountIdentity_.value(); }
    const auto &mountedPaths() const { return mountedPaths_; }
    void mountDirectory(const std::filesystem::path &path);
    // Adds every regular file below root as <prefix>/<relative path>. The PD2 CN
    // client ships its Chinese tables, fonts and menu art as loose files under
    // Data/local instead of inside an archive; a missing or empty root is a hard
    // error rather than a silent loss of the profile's resources.
    void mountLooseDirectory(const std::filesystem::path &root, std::string_view prefix);
    Bytes read(const std::string &name, bool required = true) const;
    template<class T, class Loader> std::shared_ptr<const T> prepared(Loader &&loader) const {
        const auto key = std::type_index(typeid(T));
        if (const auto found = prepared_.find(key); found != prepared_.end())
            return std::static_pointer_cast<const T>(found->second);
        auto value = std::make_shared<const T>(std::forward<Loader>(loader)());
        prepared_.emplace(key, value); // Publish only a completely loaded value.
        return value;
    }
    void setLoadingPulse(std::function<void()> pulse) { loadingPulse = std::move(pulse); }
    void pulseLoading() const { if (loadingPulse) loadingPulse(); }
    bool contains(const std::string &name) const;
    std::vector<std::string> list(const std::string &pattern = "*") const;
    void packUsed(const std::filesystem::path &destination) const;
    // Packs loose files (e.g. a mounted game's Data folder) into a new MPQ.
    // Member names are prefix + path relative to source, normalized.
    void packDirectory(const std::filesystem::path &source, std::string_view prefix,
                       const std::filesystem::path &destination) const;
};
} // namespace d2x

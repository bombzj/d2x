#pragma once
#include "contracts/automap_discovery.hpp"
#include <filesystem>
#include <optional>
#include <string_view>

namespace d2x::hosting {
// Local/TCP characters use the selected D2S basename. Realm caches belong to
// the client save root / realm / character, independently of server D2S files.
class AutomapStore {
    std::filesystem::path path_;
  public:
    explicit AutomapStore(std::filesystem::path character);
    static AutomapStore realm(const std::filesystem::path &saveRoot,
                              std::string_view realm, std::string_view character);
    std::optional<AutomapDiscovery> load(uint32_t seed, uint32_t objectSeed) const;
    void save(const AutomapDiscovery &) const;
};
} // namespace d2x::hosting

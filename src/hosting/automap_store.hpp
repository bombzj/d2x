#pragma once
#include "contracts/automap_discovery.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace d2x::hosting {
// Captured from a selected local character lease; callers never resolve UI names to paths.
class AutomapStore {
    std::filesystem::path path_;
    std::string name_, characterClass_;

  public:
    AutomapStore(std::filesystem::path character, std::string name, std::string characterClass);
    std::optional<AutomapDiscovery> load(bool tcpIp, int difficulty, int act, uint32_t seed,
                                         uint64_t content) const;
    void save(bool tcpIp, const AutomapDiscovery &) const;
};
} // namespace d2x::hosting

#pragma once
#include "core/bytes.hpp"
#include "path.hpp"
#include <filesystem>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <utility>
namespace d2x {
Bytes readFile(const std::filesystem::path &path);
void writeFile(const std::filesystem::path &path, std::span<const uint8_t> data);
class Archives {
    std::vector<void *> handles;
  std::function<void()> loadingPulse;

  public:
    std::vector<std::string> names;
    mutable std::set<std::string> used;
    Archives() = default;
    Archives(const Archives &) = delete;
    Archives &operator=(const Archives &) = delete;
    ~Archives();
    void mount(const std::filesystem::path &path);
    void mountDirectory(const std::filesystem::path &path);
    Bytes read(const std::string &name, bool required = true) const;
    void setLoadingPulse(std::function<void()> pulse) { loadingPulse = std::move(pulse); }
    void pulseLoading() const { if (loadingPulse) loadingPulse(); }
    bool contains(const std::string &name) const;
    std::vector<std::string> list(const std::string &pattern = "*") const;
    void packUsed(const std::filesystem::path &destination) const;
};
} // namespace d2x

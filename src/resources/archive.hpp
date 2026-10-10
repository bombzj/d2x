#pragma once
#include "core/bytes.hpp"
#include "path.hpp"
#include <filesystem>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <map>
#include <memory>
#include <typeindex>
namespace d2x {
Bytes readFile(const std::filesystem::path &path);
void writeFile(const std::filesystem::path &path, std::span<const uint8_t> data);
class Archives {
    std::vector<void *> handles;
    std::function<void()> loadingPulse;
    mutable bool listed_{};
    // Owned by this mounted resource set, not by a player, room or connection.
    mutable std::map<std::type_index, std::shared_ptr<const void>> prepared_;
    mutable std::map<std::string, Bytes, std::less<>> readCache_;
    mutable size_t cacheBytes_{};

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
};
} // namespace d2x

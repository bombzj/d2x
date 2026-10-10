#pragma once
#include "core/bytes.hpp"
#include "hosting/character_directory.hpp"
#include "gameplay/character/persistent_character.hpp"
#include <filesystem>
#include <map>
#include <string_view>

namespace d2x {
struct ClassicData;
// Server-side repository. UI identities never become filesystem paths.
class CharacterStore {
  public:
    struct Lease {
        std::filesystem::path path, lock;
        std::vector<uint8_t> expected;
        ~Lease();
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(std::filesystem::path, std::filesystem::path);
    };
    CharacterStore(std::filesystem::path root, const ClassicData &);
    CharacterRosterView list(bool includeQuick = false, std::string_view fileName = {});
    void install(std::string fileName, const PersistentCharacter &);
    void create(std::string name, unsigned characterClass, uint32_t seed);
    void erase(uint64_t revision, uint64_t id);
    std::unique_ptr<Lease> acquire(uint64_t revision, uint64_t id);
    PersistentCharacter load(Lease &);
    void save(Lease &, const PersistentCharacter &);
    void saveBytes(Lease &, Bytes); // Validated native TCP/IP return; preserve original bytes.
  private:
    const ClassicData &content_;
    std::filesystem::path root_;
    uint64_t revision_{};
    std::map<uint64_t, std::filesystem::path> entries_;
    std::map<uint64_t, std::vector<uint8_t>> versions_;
    std::filesystem::path resolve(uint64_t revision, uint64_t id) const;
    std::unique_ptr<Lease> lock(const std::filesystem::path &) const;
};
} // namespace d2x

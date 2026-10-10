#include "character_store.hpp"
#include "character_creation.hpp"
#include "content/classic_data.hpp"
#include "persistence/save_codec.hpp"
#include "resources/atomic_file.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
std::string folded(std::string value) {
    for (auto &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
Bytes readBytes(const std::filesystem::path &path) {
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("Character save must be a regular file, not a link");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open character save");
    const auto size = file.tellg();
    if (size < 0 || uint64_t(size) > maxSaveBytes) throw std::runtime_error("Invalid character save size");
    Bytes bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot read character save");
    return bytes;
}
}
CharacterStore::Lease::Lease(std::filesystem::path file, std::filesystem::path directory)
    : path(std::move(file)), lock(std::move(directory)) {}
CharacterStore::Lease::~Lease() { if (!lock.empty()) { std::error_code ec; std::filesystem::remove(lock, ec); } }
CharacterStore::CharacterStore(std::filesystem::path root, const ClassicData &content) : content_(content) {
    std::filesystem::create_directories(root);
    root_ = std::filesystem::canonical(root);
}
std::unique_ptr<CharacterStore::Lease> CharacterStore::lock(const std::filesystem::path &path) const {
    if (path.parent_path() != root_) throw std::runtime_error("Character path escaped repository");
    const auto directory = root_ / (folded(path.stem().string()) + ".character-lock");
    if (!std::filesystem::create_directory(directory)) {
        throw std::runtime_error("Character is in use (or has a stale .character-lock directory)");
    }
    try { return std::make_unique<Lease>(path, directory); }
    catch (...) { std::error_code ec; std::filesystem::remove(directory, ec); throw; }
}
CharacterRosterView CharacterStore::list(bool includeQuick, std::string_view fileName) {
    CharacterRosterView view;
    view.revision = ++revision_;
    entries_.clear();
    versions_.clear();
    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::directory_iterator(root_))
        if (folded(entry.path().extension().string()) == ".d2s" && (includeQuick || folded(entry.path().stem().string()) != "quick") &&
            (fileName.empty() || entry.path().filename().string() == fileName))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return folded(a.filename().string()) < folded(b.filename().string()); });
    std::map<std::string, unsigned> names, stems;
    for (const auto &path : files) ++stems[folded(path.stem().string())];
    for (const auto &path : files) {
        CharacterRosterEntry entry;
        entry.id = entries_.size() + 1;
        entry.name = path.stem().string();
        entry.fileName = path.filename().string();
        entries_.emplace(entry.id, path);
        try {
            auto bytes = readBytes(path);
            versions_.emplace(entry.id, bytes);
            const auto saved = decodeSave(bytes, content_);
            entry.nativeStatus = uint16_t(bytes.at(0x24)) | uint16_t(bytes.at(0x25)) << 8;
            entry.name = saved.player.name;
            if (!validCharacterName(entry.name)) throw std::runtime_error("Unsupported character name");
            ++names[folded(entry.name)];
            const auto found = std::find_if(content_.characters.begin(), content_.characters.end(), [&](const auto &c) { return c.name == saved.player.characterClass; });
            if (found == content_.characters.end()) throw std::runtime_error("Unsupported character class");
            entry.characterClass = unsigned(found - content_.characters.begin());
            entry.level = unsigned(saved.player.level);
            entry.appearance = characterAppearance(content_, saved);
            if (stems.at(folded(path.stem().string())) != 1) throw std::runtime_error("Case-insensitive filename collision");
            if (std::filesystem::exists(root_ / (folded(path.stem().string()) + ".character-lock")))
                throw std::runtime_error("Character is in use (or its lock needs manual recovery)");
            entry.playable = true;
        } catch (const std::exception &e) { entry.problem = e.what(); }
        view.characters.push_back(std::move(entry));
    }
    for (auto &entry : view.characters)
        if (names[folded(entry.name)] > 1) { entry.playable = false; entry.problem = "Duplicate saved character name"; }
    return view;
}
std::filesystem::path CharacterStore::resolve(uint64_t revision, uint64_t id) const {
    if (revision != revision_ || !entries_.contains(id)) throw std::runtime_error("Character list changed; refresh and select again");
    return entries_.at(id);
}
std::unique_ptr<CharacterStore::Lease> CharacterStore::acquire(uint64_t revision, uint64_t id) {
    const auto path = resolve(revision, id);
    auto lease = lock(path);
    lease->expected = readBytes(path);
    if (!versions_.contains(id) || versions_.at(id) != lease->expected)
        throw std::runtime_error("Character file changed since listing; refresh before selecting or deleting");
    return lease;
}
PersistentCharacter CharacterStore::load(Lease &lease) {
    const auto bytes = readBytes(lease.path);
    if (bytes != lease.expected) throw std::runtime_error("Character save changed outside this host; reload was refused");
    return decodeSave(bytes, content_);
}
void CharacterStore::save(Lease &lease, const PersistentCharacter &character) {
    auto bytes = encodeSave(character, content_);
    decodeSave(bytes, content_); // Existing codec's validation before atomic replacement.
    if (readBytes(lease.path) != lease.expected) throw std::runtime_error("Character save changed outside this host; overwrite was refused");
    writeFileAtomically(lease.path, bytes, true);
    lease.expected = std::move(bytes);
}
void CharacterStore::saveBytes(Lease &lease, Bytes bytes) {
    const auto returned = decodeSave(bytes, content_);
    const auto original = decodeSave(lease.expected, content_);
    if (returned.player.name != original.player.name || returned.player.characterClass != original.player.characterClass)
        throw std::runtime_error("TCP/IP returned a different character; local save was not replaced");
    if (readBytes(lease.path) != lease.expected) throw std::runtime_error("Character changed outside TCP/IP; overwrite was refused");
    writeFileAtomically(lease.path, bytes, true);
    lease.expected = std::move(bytes);
}
void CharacterStore::create(std::string name, unsigned characterClass, uint32_t seed) {
    if (!validCharacterName(name) || folded(name) == "quick") throw std::runtime_error("Use 1-15 letters, digits or hyphens; quick is reserved");
    // Windows device names must be rejected on every platform for portable saves.
    const auto key = folded(name);
    if (key == "con" || key == "prn" || key == "aux" || key == "nul" ||
        (key.size() == 4 && (key.starts_with("com") || key.starts_with("lpt")) && key[3] >= '1' && key[3] <= '9'))
        throw std::runtime_error("This character name is reserved by the filesystem");
    const auto path = root_ / (name + ".d2s");
    auto lease = lock(path);
    for (const auto &entry : std::filesystem::directory_iterator(root_)) {
        if (folded(entry.path().extension().string()) != ".d2s") continue;
        if (folded(entry.path().stem().string()) == key) throw std::runtime_error("A character with this name already exists");
        // A renamed file must not bypass character-name uniqueness. Invalid
        // unrelated files remain visible and do not prevent character creation.
        std::optional<PersistentCharacter> saved;
        try { saved = decodeSave(readBytes(entry.path()), content_); } catch (const std::exception &) {}
        if (saved && folded(saved->player.name) == key) throw std::runtime_error("A saved character already uses this name");
    }
    auto bytes = encodeSave(createCharacter(content_, std::move(name), characterClass, seed), content_);
    decodeSave(bytes, content_);
    if (std::filesystem::exists(path)) throw std::runtime_error("Character file appeared during creation");
    writeFileAtomically(path, bytes, false);
}
void CharacterStore::erase(uint64_t revision, uint64_t id) {
    auto lease = acquire(revision, id);
    // Keep existing .bak and client exploration sidecars, as master did. Only
    // the explicitly selected D2S is deleted; never recursively delete a name.
    if (readBytes(lease->path) != lease->expected || !std::filesystem::remove(lease->path))
        throw std::runtime_error("Character could not be deleted");
}
void CharacterStore::install(std::string fileName, const PersistentCharacter &state) {
    const std::filesystem::path relative(fileName);
    if (relative.has_parent_path() || folded(relative.extension().string()) != ".d2s")
        throw std::runtime_error("Startup save must name a D2S file in its repository");
    const auto path = root_ / relative;
    auto lease = lock(path);
    if (std::filesystem::exists(path)) throw std::runtime_error("Startup destination already exists; use --load to continue it");
    auto bytes = encodeSave(state, content_); decodeSave(bytes, content_);
    writeFileAtomically(path, bytes, false);
}
} // namespace d2x

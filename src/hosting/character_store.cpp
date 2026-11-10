#include "gameplay/character/persistent_character.hpp"
#include "character_store.hpp"
#include "character_creation.hpp"
#include "content/classic_data.hpp"
#include "persistence/save_codec.hpp"
#include "persistence/save_file.hpp"
#include "persistence/d2s_header.hpp"
#include "resources/atomic_file.hpp"
#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
std::string folded(std::string value) {
    for (auto &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
}
CharacterStore::Lease::Lease(std::filesystem::path file, std::unique_ptr<FileLock> owner)
    : path(std::move(file)), lock(std::move(owner)) {}
CharacterStore::CharacterStore(std::filesystem::path root, const ClassicData &content) : content_(content) {
    std::filesystem::create_directories(root);
    root_ = std::filesystem::canonical(root);
}
std::unique_ptr<CharacterStore::Lease> CharacterStore::lock(const std::filesystem::path &path) const {
    if (path.parent_path() != root_) throw std::runtime_error("Character path escaped repository");
    auto owner = FileLock::tryAcquire(characterLeasePath(path));
    if (!owner) throw std::runtime_error("Character is in use by another session");
    return std::make_unique<Lease>(path, std::move(owner));
}
CharacterRosterView CharacterStore::list(bool includeQuick, std::string_view fileName) {
    CharacterRosterView view;
    view.revision = ++revision_;
    entries_.clear();
    versions_.clear();
    struct SaveFile {
        std::filesystem::path path;
        std::optional<std::filesystem::file_time_type> modified;
    };
    std::vector<SaveFile> files;
    for (const auto &entry : std::filesystem::directory_iterator(root_))
        if (folded(entry.path().extension().string()) == ".d2s" && (includeQuick || folded(entry.path().stem().string()) != "quick") &&
            (fileName.empty() || entry.path().filename().string() == fileName)) {
            std::error_code error;
            const auto modified = entry.last_write_time(error);
            files.push_back({entry.path(), error ? std::nullopt : std::optional{modified}});
        }
    // LoD D2Launch CharSel (1.13c RVA 0xCC50 / 0xCA41) orders local saves
    // by file last-write time, newest first; equal times keep enumeration order.
    // Missing metadata must not hide an existing invalid/unavailable save entry.
    std::stable_sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.modified > b.modified; });
    std::map<std::string, unsigned> names, stems;
    for (const auto &file : files) ++stems[folded(file.path.stem().string())];
    for (const auto &file : files) {
        const auto &path = file.path;
        CharacterRosterEntry entry;
        entry.id = entries_.size() + 1;
        entry.name = path.stem().string();
        entry.fileName = path.filename().string();
        entries_.emplace(entry.id, path);
        try {
            auto available = lock(path);
            const auto &files = versions_.emplace(entry.id, readSaveFiles(path, content_, true)).first->second;
            const auto saved = decodeSave(combinePd2Save(files, content_), content_);
            entry.nativeStatus = uint16_t(files.main.at(0x24)) | uint16_t(files.main.at(0x25)) << 8;
            entry.name = saved.player.name;
            if (!validCharacterName(entry.name)) throw std::runtime_error("Unsupported character name");
            ++names[folded(entry.name)];
            const auto found = std::find_if(content_.characters.begin(), content_.characters.end(), [&](const auto &c) { return c.name == saved.player.characterClass; });
            if (found == content_.characters.end()) throw std::runtime_error("Unsupported character class");
            entry.characterClass = unsigned(found - content_.characters.begin());
            entry.level = unsigned(saved.player.level);
            entry.appearance = characterAppearance(content_, saved);
            if (stems.at(folded(path.stem().string())) != 1) throw std::runtime_error("Case-insensitive filename collision");
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
    lease->expected = readSaveFiles(path, content_, true);
    if (!versions_.contains(id) || versions_.at(id) != lease->expected)
        throw std::runtime_error("Character file changed since listing; refresh before selecting or deleting");
    return lease;
}
void CharacterStore::checkCurrent(Lease &lease) {
    const auto files = readSaveFiles(lease.path, content_, true);
    if (lease.pending && files == *lease.pending) lease.expected = std::move(*lease.pending);
    lease.pending.reset();
    if (files != lease.expected) throw std::runtime_error("Character save changed outside this host");
}
PersistentCharacter CharacterStore::load(Lease &lease, std::optional<int> gameDifficulty) {
    checkCurrent(lease);
    return decodeSave(combinePd2Save(lease.expected, content_), content_, gameDifficulty);
}
Bytes CharacterStore::transferableSave(Lease &lease) {
    checkCurrent(lease);
    return combinePd2Save(lease.expected, content_);
}
void CharacterStore::save(Lease &lease, const PersistentCharacter &character) {
    saveBytes(lease, encodeSave(character, content_));
}
void CharacterStore::saveBytes(Lease &lease, Bytes bytes) {
    checkCurrent(lease);
    const auto character = decodeSave(bytes, content_);
    const auto original = decodeSave(combinePd2Save(lease.expected, content_), content_);
    if (character.player.name != original.player.name || character.player.characterClass != original.player.characterClass ||
        character.player.hardcore != original.player.hardcore || (original.player.hardcoreDead && !character.player.hardcoreDead))
        throw std::runtime_error("Character identity or permanent death changed; save was refused");
    auto after = lease.expected.extended ? splitPd2Save(bytes, *lease.expected.extended, content_) : CharacterSaveFiles{std::move(bytes), {}};
    if (after.extended) decodeSave(combinePd2Save(after, content_), content_);
    lease.pending = std::move(after);
    writeSaveFiles(lease.path, lease.expected, *lease.pending);
    lease.expected = std::move(*lease.pending);
    lease.pending.reset();
}
void CharacterStore::create(std::string name, unsigned characterClass, uint32_t seed, bool hardcore) {
    if (!validCharacterName(name) || folded(name) == "quick") throw std::runtime_error("Use 1-15 letters, digits, hyphens or underscores; quick is reserved");
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
        try { saved = decodeSave(combinePd2Save(readSaveFiles(entry.path(), content_), content_), content_); } catch (const std::exception &) {}
        if (saved && folded(saved->player.name) == key) throw std::runtime_error("A saved character already uses this name");
    }
    auto bytes = encodeSave(createCharacter(content_, std::move(name), characterClass, seed, hardcore), content_);
    if (!content_.pd2Host) {
        auto header = readD2sHeader(bytes);
        bytes.resize(d2sHeaderSize);
        header.flags |= 1u;
        writeD2sHeader(bytes, header);
    }
    decodeSave(bytes, content_);
    if (std::filesystem::exists(path)) throw std::runtime_error("Character file appeared during creation");
    if (content_.pd2Host && std::filesystem::exists(std::filesystem::path(path).replace_extension(".d2x")))
        throw std::runtime_error("An extended save already uses this character filename");
    writeFileAtomically(path, bytes, false);
}
void CharacterStore::erase(uint64_t revision, uint64_t id) {
    auto lease = acquire(revision, id);
    // Keep existing .bak and client exploration sidecars, as master did. Only
    // the explicitly selected D2S is deleted; never recursively delete a name.
    if (lease->expected.extended) throw std::runtime_error("Delete split character files together outside the game");
    if (readSaveFiles(lease->path, content_) != lease->expected || !std::filesystem::remove(lease->path))
        throw std::runtime_error("Character could not be deleted");
}
void CharacterStore::install(std::string fileName, const PersistentCharacter &state, const Bytes *extendedLayout) {
    const std::filesystem::path relative(fileName);
    if (relative.has_parent_path() || folded(relative.extension().string()) != ".d2s")
        throw std::runtime_error("Startup save must name a D2S file in its repository");
    const auto path = root_ / relative;
    auto lease = lock(path);
    if (std::filesystem::exists(path)) throw std::runtime_error("Startup destination already exists; use --load to continue it");
    if (content_.pd2Host && std::filesystem::exists(std::filesystem::path(path).replace_extension(".d2x")))
        throw std::runtime_error("Startup extended save already exists");
    auto bytes = encodeSave(state, content_);
    auto after = extendedLayout ? splitPd2Save(bytes, *extendedLayout, content_) : CharacterSaveFiles{std::move(bytes), {}};
    decodeSave(combinePd2Save(after, content_), content_);
    writeSaveFiles(path, {}, after);
}
} // namespace d2x

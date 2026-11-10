#include "save_file.hpp"
#include "save_codec.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace d2x {
namespace {
auto extendedPath(std::filesystem::path path) { return path.replace_extension(".d2x"); }
auto journalPath(std::filesystem::path path) { return path.replace_extension(".d2x-pending"); }
std::optional<Bytes> readOptionalSave(const std::filesystem::path &path, size_t limit = maxSaveBytes) {
    const auto status = std::filesystem::symlink_status(path);
    if (status.type() == std::filesystem::file_type::not_found) return {};
    if (!std::filesystem::is_regular_file(status)) throw std::runtime_error("Character save must be a regular file: " + path.string());
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open save: " + path.string());
    auto length = file.tellg();
    if (length < 0 || uint64_t(length) > limit)
        throw std::runtime_error("Invalid save file size");
    Bytes bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot read save: " + path.string());
    return bytes;
}
CharacterSaveFiles current(const std::filesystem::path &path, bool pd2) {
    auto main = readOptionalSave(path);
    return {main ? std::move(*main) : Bytes{}, pd2 ? readOptionalSave(extendedPath(path)) : std::nullopt};
}
constexpr std::array<uint8_t, 8> journalHeader{'D','2','X','P','A','I','R',1};
void appendJournalField(Bytes &record, std::span<const uint8_t> bytes) {
    if (bytes.size() > maxSaveBytes) throw std::runtime_error("Oversized split save component");
    for (unsigned shift : {0u,8u,16u,24u}) record.push_back(uint8_t(bytes.size() >> shift));
    record.insert(record.end(), bytes.begin(), bytes.end());
}
Bytes journalField(std::span<const uint8_t> &bytes) {
    if (bytes.size() < 4) throw std::runtime_error("Truncated split save recovery record");
    const uint32_t size = bytes[0] | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    bytes = bytes.subspan(4);
    if (size > maxSaveBytes || size > bytes.size()) throw std::runtime_error("Invalid split save recovery size");
    Bytes result(bytes.begin(), bytes.begin() + size);
    bytes = bytes.subspan(size);
    return result;
}
void finishPair(const std::filesystem::path &path, const CharacterSaveFiles &before, const CharacterSaveFiles &after) {
    const auto disk = current(path, true);
    if ((disk.main != before.main && disk.main != after.main) ||
        (disk.extended != before.extended && disk.extended != after.extended))
        throw std::runtime_error("Split character files changed outside this host; pending save was retained");
    if (before.extended) {
        auto mainBackup = path; mainBackup += ".bak";
        auto extendedBackup = extendedPath(path); extendedBackup += ".bak";
        writeFileAtomically(mainBackup, before.main, false);
        writeFileAtomically(extendedBackup, *before.extended, false);
    }
    if (disk.extended != after.extended) writeFileAtomically(extendedPath(path), *after.extended, false);
    if (disk.main != after.main) writeFileAtomically(path, after.main, false);
    if (!std::filesystem::remove(journalPath(path))) throw std::runtime_error("Split save recovery record disappeared");
}
void recoverPair(const std::filesystem::path &path, const Bytes &record, const ClassicData &content) {
    if (record.size() < journalHeader.size() || !std::equal(journalHeader.begin(), journalHeader.end(), record.begin()))
        throw std::runtime_error("Invalid split save recovery header");
    auto bytes = std::span<const uint8_t>(record).subspan(journalHeader.size());
    auto oldMain = journalField(bytes), oldExtra = journalField(bytes);
    auto newMain = journalField(bytes), newExtra = journalField(bytes);
    if (!bytes.empty() || newMain.empty() || newExtra.empty() || oldMain.empty() != oldExtra.empty())
        throw std::runtime_error("Invalid split save recovery record");
    const CharacterSaveFiles after{std::move(newMain), std::move(newExtra)};
    decodeSave(combinePd2Save(after, content), content);
    finishPair(path, {std::move(oldMain), oldExtra.empty() ? std::nullopt : std::optional{std::move(oldExtra)}}, after);
}
} // namespace

std::filesystem::path characterLeasePath(const std::filesystem::path &path) {
    auto stem = path.stem().string();
    for (auto &c : stem) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return path.parent_path() / (stem + ".character-lock");
}

CharacterSaveFiles readSaveFiles(const std::filesystem::path &path, const ClassicData &content, bool recover) {
    if (content.pd2Host) if (auto record = readOptionalSave(journalPath(path), 4 * maxSaveBytes + 24)) {
        if (!recover) throw std::runtime_error("Split character has a pending save; reopen with its character lease");
        recoverPair(path, *record, content);
    }
    auto files = current(path, content.pd2Host);
    if (files.main.empty()) throw std::runtime_error("Missing or empty main character file: " + path.string());
    return files;
}

void writeSaveFiles(const std::filesystem::path &path, const CharacterSaveFiles &before, const CharacterSaveFiles &after) {
    if (!after.extended) {
        if (before.extended) throw std::runtime_error("Cannot replace a split character with a main-only file");
        const auto disk = readOptionalSave(path);
        if (disk ? *disk != before.main : !before.main.empty())
            throw std::runtime_error("Character save changed before saving");
        writeFileAtomically(path, after.main, !before.main.empty());
        return;
    }
    if (readOptionalSave(journalPath(path), 4 * maxSaveBytes + 24)) throw std::runtime_error("A split save is already pending");
    if (current(path, true) != before) throw std::runtime_error("Split character files changed before saving");
    Bytes record(journalHeader.begin(), journalHeader.end());
    appendJournalField(record, before.main);
    appendJournalField(record, before.extended ? std::span<const uint8_t>(*before.extended) : std::span<const uint8_t>{});
    appendJournalField(record, after.main);
    appendJournalField(record, *after.extended);
    writeFileAtomically(journalPath(path), record, false);
    finishPair(path, before, after);
}

CharacterSaveData loadSave(const std::filesystem::path &path, const ClassicData &content, CharacterSaveFiles *source) {
    auto lease = content.pd2Host ? FileLock::tryAcquire(characterLeasePath(path)) : nullptr;
    if (content.pd2Host && !lease) throw std::runtime_error("Character is in use by another session");
    auto files = readSaveFiles(path, content, bool(lease));
    auto decoded = decodeSave(combinePd2Save(files, content), content);
    if (source) *source = std::move(files);
    return decoded;
}
void writeSave(const std::filesystem::path &path, const CharacterSaveData &snapshot, const ClassicData &content) {
    if (path.empty() || path.filename().empty())
        throw std::runtime_error("Save path must name a file");
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return char(std::tolower(character));
    });
    if (extension != ".d2s")
        throw std::runtime_error("Character saves require the .d2s extension");
    auto bytes = encodeSave(snapshot, content);
    auto lease = FileLock::tryAcquire(characterLeasePath(path));
    if (!lease) throw std::runtime_error("Character is in use by another session");
    const auto before = std::filesystem::exists(path) || (content.pd2Host && std::filesystem::exists(journalPath(path)))
        ? readSaveFiles(path, content, true) : current(path, content.pd2Host);
    if (before.main.empty() && before.extended) throw std::runtime_error("Extended save has no main character file");
    auto after = before.extended ? splitPd2Save(bytes, *before.extended, content) : CharacterSaveFiles{std::move(bytes), {}};
    decodeSave(combinePd2Save(after, content), content);
    writeSaveFiles(path, before, after);
}
} // namespace d2x

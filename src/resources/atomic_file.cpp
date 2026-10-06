#include "atomic_file.hpp"
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif
namespace d2x {
namespace {
// Only this filesystem boundary knows how to atomically replace an existing file.
void replaceFile(const std::filesystem::path &from, const std::filesystem::path &to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(int(GetLastError()), std::system_category(), "Cannot replace save file");
#else
    std::filesystem::rename(from, to);
#endif
}
class SaveLock {
  public:
    std::filesystem::path directory;
    explicit SaveLock(std::filesystem::path path) : directory(std::move(path)) {
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Save is locked. Close other D2X instances; inspect " +
                                     directory.string());
    }
    ~SaveLock() {
        std::error_code ignored;
        // Never recursively remove a path supplied from outside this operation.
        std::filesystem::remove(directory / "snapshot.tmp", ignored);
        std::filesystem::remove(directory / "backup.tmp", ignored);
        std::filesystem::remove(directory, ignored);
    }
};
} // namespace
void writeFileAtomically(const std::filesystem::path &path, std::span<const uint8_t> bytes,
                         bool keepBackup) {
    if (path.empty() || path.filename().empty())
        throw std::runtime_error("Output path must name a file");
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    auto lockPath = path;
    lockPath += ".lock";
    SaveLock lock(lockPath);
    auto temporary = lock.directory / "snapshot.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        output.flush();
        if (!output)
            throw std::runtime_error("Cannot write temporary save");
        output.close();
        if (!output)
            throw std::runtime_error("Cannot close temporary save");
    }
    if (keepBackup && std::filesystem::exists(path)) {
        // Prepare the backup before replacement; failure leaves the current save intact.
        auto backup = path;
        backup += ".bak";
        auto temporaryBackup = lock.directory / "backup.tmp";
        std::filesystem::copy_file(path, temporaryBackup);
        replaceFile(temporaryBackup, backup);
    }
    replaceFile(temporary, path);
}
} // namespace d2x

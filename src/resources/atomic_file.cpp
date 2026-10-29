#include "atomic_file.hpp"
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace d2x {
struct FileLock::Impl {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool locked = false;
    ~Impl() {
        if (handle == INVALID_HANDLE_VALUE) return;
        if (locked) {
            OVERLAPPED offset{};
            UnlockFileEx(handle, 0, 1, 0, &offset);
        }
        CloseHandle(handle);
    }
#else
    int descriptor = -1;
    ~Impl() { if (descriptor >= 0) close(descriptor); }
#endif
};
FileLock::FileLock(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileLock::~FileLock() = default;
std::unique_ptr<FileLock> FileLock::tryAcquire(const std::filesystem::path &path) {
    const auto status = std::filesystem::symlink_status(path);
    if (std::filesystem::is_directory(status))
        throw std::runtime_error("Legacy lock directory requires recovery after closing all older D2X instances: " + path.string());
    if (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))
        throw std::runtime_error("Lock must be a regular file, not a link: " + path.string());
    auto impl = std::make_unique<Impl>();
#ifdef _WIN32
    impl->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (impl->handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION) return {};
        throw std::system_error(int(error), std::system_category(), "Cannot open lock file: " + path.string());
    }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(impl->handle, &information))
        throw std::system_error(int(GetLastError()), std::system_category(), "Cannot inspect lock file");
    if (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Lock must be a regular file, not a link: " + path.string());
    OVERLAPPED offset{};
    if (!LockFileEx(impl->handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &offset)) {
        const auto error = GetLastError();
        if (error == ERROR_LOCK_VIOLATION) return {};
        throw std::system_error(int(error), std::system_category(), "Cannot acquire lock file: " + path.string());
    }
    impl->locked = true;
#else
    impl->descriptor = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (impl->descriptor < 0) {
        const int error = errno;
        throw std::system_error(error, std::generic_category(), "Cannot open lock file: " + path.string());
    }
    struct stat information{};
    if (fstat(impl->descriptor, &information) != 0)
        throw std::system_error(errno, std::generic_category(), "Cannot inspect lock file");
    if (!S_ISREG(information.st_mode)) throw std::runtime_error("Lock must be a regular file: " + path.string());
    if (flock(impl->descriptor, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        if (error == EWOULDBLOCK || error == EAGAIN) return {};
        throw std::system_error(error, std::generic_category(), "Cannot acquire lock file: " + path.string());
    }
#endif
    return std::unique_ptr<FileLock>(new FileLock(std::move(impl)));
}
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
    std::unique_ptr<FileLock> owner_;
    static void removeTemporary(const std::filesystem::path &path) {
        const auto status = std::filesystem::symlink_status(path);
        if (!std::filesystem::exists(status)) return;
        if (!std::filesystem::is_regular_file(status))
            throw std::runtime_error("Temporary output must be a regular file: " + path.string());
        std::filesystem::remove(path);
    }
  public:
    std::filesystem::path temporary, backup;
    explicit SaveLock(const std::filesystem::path &path) {
        auto lockPath = path; lockPath += ".lock";
        owner_ = FileLock::tryAcquire(lockPath);
        if (!owner_) throw std::runtime_error("Output is in use by another operation: " + path.string());
        temporary = path; temporary += ".snapshot.tmp";
        backup = path; backup += ".backup.tmp";
        // Only these operation-owned files can be left by an interrupted write.
        // The previous owner cannot still be using them once its OS lock is free.
        removeTemporary(temporary);
        removeTemporary(backup);
    }
    ~SaveLock() {
        std::error_code ignored;
        // Never recursively remove a path supplied from outside this operation.
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(backup, ignored);
    }
};
} // namespace
void writeFileAtomically(const std::filesystem::path &path, std::span<const uint8_t> bytes,
                         bool keepBackup) {
    if (path.empty() || path.filename().empty())
        throw std::runtime_error("Output path must name a file");
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    SaveLock lock(path);
    const auto &temporary = lock.temporary;
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
        const auto &temporaryBackup = lock.backup;
        std::filesystem::copy_file(path, temporaryBackup);
        // copy_file preserves the source's read-only bit on Windows. Our
        // generated backup must remain replaceable after a failed write to
        // a temporarily protected save; never change the source's permissions.
        std::filesystem::permissions(temporaryBackup,std::filesystem::perms::owner_write,
            std::filesystem::perm_options::add);
        replaceFile(temporaryBackup, backup);
    }
    replaceFile(temporary, path);
}
} // namespace d2x

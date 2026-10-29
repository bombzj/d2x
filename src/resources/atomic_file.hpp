#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
namespace d2x {
// Process-held sidecar lock. Its file stays in place so separate open handles
// always contend on the same file; closing/crashing releases the OS lock.
class FileLock {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit FileLock(std::unique_ptr<Impl>);
  public:
    ~FileLock();
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
    // Null means an active owner; filesystem/legacy-directory errors throw.
    static std::unique_ptr<FileLock> tryAcquire(const std::filesystem::path &);
};
// Platform filesystem adapter; no character or save-codec dependency.
void writeFileAtomically(const std::filesystem::path &, std::span<const uint8_t>, bool keepBackup = false);
}

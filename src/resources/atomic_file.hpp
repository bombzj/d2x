#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
namespace d2x {
// Platform filesystem adapter; no character or save-codec dependency.
void writeFileAtomically(const std::filesystem::path &, std::span<const uint8_t>, bool keepBackup = false);
}

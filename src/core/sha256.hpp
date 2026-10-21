#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace d2x {
using Sha256Digest = std::array<uint8_t, 32>;
class Sha256 {
    std::array<uint32_t, 8> state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> block_{};
    uint64_t bytes_{};
    size_t buffered_{};
    void compress(std::span<const uint8_t, 64>);
  public:
    ~Sha256();
    void append(std::span<const uint8_t>);
    Sha256Digest finish() const;
};
std::string hexDigest(const Sha256Digest &);
std::string sha256Hex(std::span<const uint8_t>);
} // namespace d2x

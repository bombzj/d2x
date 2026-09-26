#pragma once
#include "core/bytes.hpp"
#include <span>
#include <stdexcept>

namespace d2x {
class D2sBitReader {
    std::span<const uint8_t> bytes_;
    size_t bit_ = 0;
  public:
    explicit D2sBitReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    size_t size() const { return (bit_ + 7) / 8; }
    uint32_t read(unsigned count) {
        if (count > 32 || bit_ + count > bytes_.size() * 8)
            throw std::runtime_error("Truncated D2S item bitstream");
        uint32_t value = 0;
        for (unsigned index = 0; index < count; ++index, ++bit_)
            value |= uint32_t((bytes_[bit_ / 8] >> (bit_ % 8)) & 1) << index;
        return value;
    }
};
class D2sBitWriter {
    Bytes bytes_;
    size_t bit_ = 0;
  public:
    void write(uint32_t value, unsigned count) {
        if (count > 32 || (count < 32 && (value >> count)))
            throw std::runtime_error("D2S item value exceeds native bit width");
        for (unsigned index = 0; index < count; ++index, ++bit_) {
            if (bit_ / 8 == bytes_.size()) bytes_.push_back(0);
            bytes_[bit_ / 8] |= uint8_t(((value >> index) & 1) << (bit_ % 8));
        }
    }
    const Bytes &bytes() const { return bytes_; }
};
} // namespace d2x
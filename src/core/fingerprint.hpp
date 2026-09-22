#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace d2x {
// Compatibility fingerprint, not a security or authenticity guarantee.
class Fingerprint {
    uint64_t value_ = UINT64_C(14695981039346656037);

  public:
    void add(std::span<const uint8_t> bytes) {
        for (auto byte : bytes) {
            value_ ^= byte;
            value_ *= UINT64_C(1099511628211);
        }
        value_ ^= bytes.size();
        value_ *= UINT64_C(1099511628211);
    }
    void add(std::string_view text) {
        add(std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
    }
    uint64_t value() const { return value_; }
};
} // namespace d2x

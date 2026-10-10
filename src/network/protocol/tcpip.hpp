#pragma once
#include "wire.hpp"
#include <optional>

namespace d2x::net::protocol {
// Native OPENCHAR/CHARSAVE transfers use 255-byte parts. The original server
// requires a character smaller than 0x2000 bytes; paths never cross this boundary.
inline constexpr size_t tcpIpSaveLimit = 0x2000;
std::vector<Bytes> tcpip_upload(std::span<const uint8_t> save);
std::vector<Bytes> tcpip_download(std::span<const uint8_t> save);
class TcpIpSaveTransfer {
    Bytes bytes_;
    uint32_t total_{};
  public:
    std::optional<Bytes> append(uint32_t total, std::span<const uint8_t> part, bool first);
    bool incomplete() const { return total_ != 0; }
    void reset() { bytes_.clear(); total_ = 0; }
};
}

#pragma once
#include "message_catalog.hpp"

namespace d2x::hosting {
class ClientPacketStream {
    Bytes bytes_;
    size_t offset_{};
  public:
    void append(std::span<const uint8_t> bytes) {
        if (bytes.size() > 256 * 1024 || bytes_.size() - offset_ > 256 * 1024 - bytes.size())
            throw net::protocol::ProtocolError("Game input queue exceeds limit");
        if (offset_) { bytes_.erase(bytes_.begin(), bytes_.begin() + offset_); offset_ = 0; }
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }
    bool next(Bytes &packet) {
        const auto data = std::span<const uint8_t>(bytes_).subspan(offset_);
        const auto length = clientPacketSize(data);
        if (!length) return false;
        packet.assign(data.begin(), data.begin() + length); offset_ += length;
        return true;
    }
    void reset() { bytes_.clear(); offset_ = 0; }
};
}

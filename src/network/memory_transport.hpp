#pragma once
#include "byte_transport.hpp"
#include <deque>
#include <mutex>

namespace d2x::net {
// Thread-safe bounded byte queues. A generation retires queued data on reconnect.
// Server polling happens on its scheduler; the RealmSession worker never calls it.
class MemoryChannel {
    friend class MemoryTransport;
    mutable std::mutex mutex_;
    uint64_t generation_{};
    bool open_{}, accepted_{};
    size_t clientBytes_{}, serverBytes_{};
    std::deque<Bytes> client_;
    std::vector<StreamEvent> server_;
  public:
    struct Input { uint64_t generation{}; bool open{}, connected{}; std::deque<Bytes> bytes; };
    Input take();
    bool send(uint64_t generation, Bytes);
    void close(uint64_t generation, std::string error = {});
};
class MemoryTransport final : public IByteTransport {
    std::shared_ptr<MemoryChannel> channel_;
  public:
    explicit MemoryTransport(std::shared_ptr<MemoryChannel> channel) : channel_(std::move(channel)) {}
    ~MemoryTransport() override { close(); }
    void connect(Endpoint, std::chrono::milliseconds) override;
    bool send(Bytes) override;
    std::vector<StreamEvent> poll() override;
    void close() override;
    bool connected() const override;
};
} // namespace d2x::net

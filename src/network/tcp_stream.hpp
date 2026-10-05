#pragma once
#include "core/bytes.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace d2x::net {
struct Endpoint {
    std::string host;
    uint16_t port{};
};
enum class StreamEventKind { Connected, Data, Closed, Error };
struct StreamEvent {
    StreamEventKind kind{};
    Bytes data;
    std::string error;
};
struct StreamLimits {
    size_t queuedSendBytes{1024 * 1024};
    size_t queuedReceiveBytes{2 * 1024 * 1024};
    size_t queuedEvents{512};
    size_t handlersPerPoll{128};
};
// Single-thread owned. poll() progresses DNS, connect, reads, writes and timers.
// Keep polling during menu pauses. No callbacks or native socket types cross this API.
class TcpStream {
  public:
    explicit TcpStream(StreamLimits limits = {});
    ~TcpStream();
    TcpStream(const TcpStream &) = delete;
    TcpStream &operator=(const TcpStream &) = delete;
    void connect(Endpoint endpoint, std::chrono::milliseconds timeout);
    bool send(Bytes bytes);
    std::vector<StreamEvent> poll();
    void close();
    bool connected() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace d2x::net

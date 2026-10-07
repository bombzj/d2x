#pragma once
#include "core/bytes.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace d2x::net {
struct Endpoint { std::string host; uint16_t port{}; };
enum class StreamEventKind { Connected, Data, Closed, Error };
struct StreamEvent { StreamEventKind kind{}; Bytes data; std::string error; };
class IByteTransport {
  public:
    virtual ~IByteTransport() = default;
    virtual void connect(Endpoint, std::chrono::milliseconds) = 0;
    virtual bool send(Bytes) = 0;
    virtual std::vector<StreamEvent> poll() = 0;
    virtual void close() = 0;
    virtual bool connected() const = 0;
};
// The protocol consumer owns one transport per native stream. Selection is
// composition only: neither side can exchange authority or decoded view values.
class ByteStream {
    std::unique_ptr<IByteTransport> transport_;
  public:
    ByteStream();
    void use(std::unique_ptr<IByteTransport> transport) { if (transport_) transport_->close(); transport_ = std::move(transport); }
    void connect(Endpoint endpoint, std::chrono::milliseconds timeout) { transport_->connect(std::move(endpoint), timeout); }
    bool send(Bytes bytes) { return transport_->send(std::move(bytes)); }
    std::vector<StreamEvent> poll() { return transport_->poll(); }
    void close() { transport_->close(); }
    bool connected() const { return transport_->connected(); }
};
} // namespace d2x::net

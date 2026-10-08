#pragma once
#include "byte_transport.hpp"
#include <array>
#include <cstdint>
namespace d2x::net {
// Transport only. Accepted sockets expose bytes; no MCP, tickets or gameplay.
class TcpListener {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    struct Event {
        uint64_t connection{};
        uint8_t listener{};
        StreamEvent stream;
        // The interface reached by this client; populated on Connected only.
        std::string localAddress;
    };
    TcpListener();
    ~TcpListener();
    void listen(std::array<Endpoint, 2> endpoints);
    std::vector<Event> poll();
    bool send(uint64_t connection, Bytes);
    void close(uint64_t connection);
    void closeAfterWrites(uint64_t connection);
    void shutdown();
};
}

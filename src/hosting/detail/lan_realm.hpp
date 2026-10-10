#pragma once
#include "native_realm_service.hpp"
#include "hosting/protocol/client_stream.hpp"
#include "network/tcp_listener.hpp"
#include <chrono>
namespace d2x::hosting {
// Original TCP/IP exposes D2GS directly, without an account/MCP listener.
class LanRealm {
    NativeRealmHost &host_;
    net::TcpListener network_;
    struct Peer {
        uint64_t socket{};
        bool closed{}, closing{}, cleanupAttempted{};
        std::chrono::steady_clock::time_point deadline;
        ClientPacketStream packets;
        std::unique_ptr<NativeRealmService> service;
    };
    std::map<uint64_t, std::unique_ptr<Peer>> peers_;
    void retire(Peer &);
    void fail(Peer &, std::string);
    void collect();
  public:
    explicit LanRealm(NativeRealmHost &host) : host_(host) {}
    ~LanRealm();
    void listen(std::string address, uint16_t gamePort);
    void poll();
    void close();
    std::optional<AdminResult> administer(const AdminRequest &);
};
}

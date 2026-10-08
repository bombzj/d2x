#pragma once
#include "realm_connection.hpp"
#include "network/tcp_listener.hpp"
#include <chrono>
namespace d2x::hosting {
class LanRealm {
    NativeRealmHost &host_;
    net::TcpListener network_;
    struct Peer {
        uint64_t realmSocket{}, gameSocket{};
        bool abandoned{}, realmClosed{}, cleanupAttempted{};
        std::chrono::steady_clock::time_point deadline;
        std::unique_ptr<RealmConnection> connection;
    };
    struct GameSocket { ClientPacketStream packets; Peer *peer{}; std::chrono::steady_clock::time_point deadline; };
    std::vector<std::unique_ptr<Peer>> peers_;
    std::map<uint64_t, Peer *> realms_;
    std::map<uint64_t, GameSocket> games_;
    void fail(Peer &, std::string);
    void collect();
  public:
    explicit LanRealm(NativeRealmHost &host) : host_(host) {}
    ~LanRealm();
    void listen(std::string address, uint16_t realmPort, uint16_t gamePort);
    void poll();
    void close();
    std::optional<AdminResult> administer(const AdminRequest &);
};
}

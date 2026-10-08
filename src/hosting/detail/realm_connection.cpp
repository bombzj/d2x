#include "realm_connection.hpp"
namespace d2x::hosting {
void RealmConnection::openRealm() { service.resetRealm(); realmPackets.reset(); selector = false; }
void RealmConnection::realm(std::span<const uint8_t> bytes) {
    if (!selector && !bytes.empty()) {
        if (bytes[0] != 1) throw net::protocol::ProtocolError("Invalid MCP selector");
        selector = true; bytes = bytes.subspan(1);
    }
    realmPackets.append(bytes); net::protocol::Packet packet;
    while (realmPackets.next(packet)) service.realm(packet);
}
void RealmConnection::openGame(bool announce) {
    gamePackets.reset(); service.connectGame(announce);
}
void RealmConnection::game(std::span<const uint8_t> bytes) {
    gamePackets.append(bytes); Bytes packet;
    while (gamePackets.next(packet)) {
        service.game(packet);
        if (service.peer.phase == GamePhase::Closed) { gamePackets.reset(); break; }
    }
}
void RealmConnection::fail(std::string error) {
    service.failure = std::move(error); service.counters.lastFailure = service.failure;
    ++service.counters.failures; service.failGame();
}
}

#pragma once
#include "native_realm_service.hpp"
#include "hosting/protocol/client_stream.hpp"
namespace d2x::hosting {
// Framing and selector state belong to the endpoint, not the shared game host.
struct RealmConnection {
    NativeRealmService service;
    net::protocol::PacketStream realmPackets{net::protocol::Framing::Mcp};
    ClientPacketStream gamePackets;
    bool selector{};
    RealmConnection(NativeRealmHost &host, NativeRealmService::RealmOutput realm, NativeRealmService::GameOutput game)
        : service(host, std::move(realm), std::move(game)) {}
    void openRealm();
    void realm(std::span<const uint8_t>);
    void openGame(bool announce = true);
    void game(std::span<const uint8_t>);
    void fail(std::string error);
};
}

#include "native_realm_service.hpp"

namespace d2x::hosting {
using namespace net::protocol;
void NativeRealmService::startup(net::protocol::Reader &in) {
    if (authenticated) throw ProtocolError("Duplicate MCP startup");
    const auto header = in.take(64);
    const auto account = in.string(64); in.finish();
    if (std::any_of(header.begin(), header.end(), [](uint8_t b) { return b != 0; }) || account != "SinglePlayer")
        throw ProtocolError("Invalid preauthenticated MCP startup");
    initialize(); authenticated = true; result(1, 0);
}
void NativeRealmService::realm(const Packet &packet) {
    auto &stats = counters.realmRequests[packet.id];
    ++stats.received;
    try {
        const auto entries = realmRequests();
        const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto &e) { return e.id == packet.id; });
        if (entry == entries.end()) throw ProtocolError("Unregistered MCP request");
        counters.lastRequest = "MCP " + std::string(entry->name);
        if (packet.id != 1 && !authenticated) throw ProtocolError("MCP startup required");
        net::protocol::Reader in(packet.body);
        switch (packet.id) {
        case 0x01: startup(in); break;
        case 0x02: createCharacter(in); break;
        case 0x03: createGame(in); break;
        case 0x04: joinGame(in); break;
        case 0x05: listGames(in); break;
        case 0x06: gameInfo(in); break;
        case 0x07: selectCharacter(in); break;
        case 0x0A: deleteCharacter(in); break;
        case 0x19: listCharacters(in); break;
        default: throw std::logic_error("MCP catalog has no handler");
        }
        in.finish();
        if (entry->support == MessageSupport::Stub) {
            ++stats.notImplemented; counters.lastResult = "not-implemented";
        } else { ++stats.completed; counters.lastResult = "handled"; }
    } catch (const ProtocolError &) { ++stats.malformed; counters.lastResult = "malformed"; throw; }
}
}

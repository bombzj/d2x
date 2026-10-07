#include "native_realm_service.hpp"
#include "hosting/protocol/gameplay_dispatch.hpp"

namespace d2x::hosting {
using namespace net::protocol;
void NativeRealmService::logon(net::protocol::Reader &in) {
    if (!ticket || !binding || !selected) throw ProtocolError("Unexpected D2GS logon");
    const auto receivedHash = in.u32(); const auto receivedToken = in.u16(); const auto characterClass = in.u8();
    const auto version = in.u32(); const auto sigA = in.u32(), sigB = in.u32(); in.u8(); const auto nameBytes = in.take(16); in.finish();
    const auto end = std::find(nameBytes.begin(), nameBytes.end(), uint8_t{});
    const std::string name(nameBytes.begin(), end);
    if (receivedHash != hash || receivedToken != token || version != 13 || sigA != 0xED5DCC50 || sigB != 0x91A519B6 ||
        name != selectedName || characterClass >= content->characters.size() || content->characters[characterClass].name != selected->player.characterClass)
        throw ProtocolError("D2GS ticket or character mismatch");
    ticket = false; peer.phase = GamePhase::LoggedOn;
    Writer out; out.u8(1); out.u8(uint8_t(gameDifficulty)); out.u16(0); out.u16(0); out.u8(1); out.u8(0); sendGame(out.release()); sendGame({2});

}
void NativeRealmService::enterEnvironment(net::protocol::Reader &in) {
    in.finish();
    if (peer.phase != GamePhase::LoggedOn) throw ProtocolError("Duplicate game admission");
    for (auto &packet : admission) sendGame(std::move(packet));
    admission.clear();
    peer.phase = GamePhase::Entered;
    peer.sequence = peer.sentRevision = peer.sentMovement = 0;
    peer.lastMotion.clear();
}
void NativeRealmService::saveAndLeave(net::protocol::Reader &in) {
    in.finish();
    const bool preparedReload = reload.has_value();
    close(!preparedReload, preparedReload);
    sendGame({0xB0});
}
void NativeRealmService::ping(net::protocol::Reader &in) {
    in.u32(); in.u32(); in.u32(); in.finish();
    Bytes pong(33, 0); pong[0] = 0x8F; sendGame(std::move(pong));
}
void NativeRealmService::game(std::span<const uint8_t> bytes) {
    if (bytes.empty()) throw ProtocolError("Empty native client packet");
    auto &stats = counters.gameRequests[bytes[0]];
    ++stats.received;
    try {
        const auto &entry = clientMessage(bytes[0]);
        counters.lastRequest = "D2GS " + std::string(entry.name);
        if (clientPacketSize(bytes) != bytes.size()) throw ProtocolError("Invalid native request size");
        requirePhase(entry, peer.phase);
        if (!binding) throw ProtocolError("Native request has no game binding");
        net::protocol::Reader in(bytes); const auto id = ClientMessage(in.u8());
        if (entry.domain == MessageDomain::Lifecycle) {
            switch (id) {
            case ClientMessage::Logon: logon(in); break;
            case ClientMessage::EnterEnvironment: enterEnvironment(in); break;
            case ClientMessage::SaveAndLeave: saveAndLeave(in); break;
            case ClientMessage::Ping: ping(in); break;
            default: throw std::logic_error("Lifecycle catalog has no handler");
            }
            in.finish(); ++stats.completed; counters.lastResult = "handled";
            return;
        }
        GameplayContext context{host, *binding, terrain.origin, peer.sequence};
        const auto result = dispatchGameplay(bytes, context);
        if (!result.operation.empty()) counters.lastRequest += "/" + std::string(result.operation);
        switch (result.status) {
        case RequestStatus::Queued: ++stats.queued; counters.lastResult = "queued"; break;
        case RequestStatus::NotImplemented:
            ++stats.notImplemented; counters.lastResult = "not-implemented"; break;
        case RequestStatus::Rejected:
            ++stats.rejected; counters.lastResult = "rejected";
            // This is an authoritative position reply, never a fabricated generic ACK.
            if (entry.domain == MessageDomain::Movement) {
                counters.lastResult += "/" + std::string(commandStatusName(result.command));
                publishMotion(true);
            }
            break;
        }
    } catch (const ProtocolError &) { ++stats.malformed; counters.lastResult = "malformed"; throw; }
}
}

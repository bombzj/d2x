#include "native_realm_service.hpp"
#include "hosting/protocol/gameplay_dispatch.hpp"
#include "persistence/save_codec.hpp"
#include "hosting/character_creation.hpp"

namespace d2x::hosting {
using namespace net::protocol;
void NativeRealmService::logon(net::protocol::Reader &in) {
    if (!directTcpIp && (!ticket || !binding || !selected)) throw ProtocolError("Unexpected D2GS logon");
    const auto receivedHash = in.u32(); const auto receivedToken = in.u16(); const auto characterClass = in.u8();
    const auto version = in.u32(); const auto sigA = in.u32(), sigB = in.u32(); in.u8(); const auto nameBytes = in.take(16); in.finish();
    const auto end = std::find(nameBytes.begin(), nameBytes.end(), uint8_t{});
    const std::string name(nameBytes.begin(), end);
    if (directTcpIp) {
        if (binding || end == nameBytes.end() || !validCharacterName(name) || receivedToken != 1 || version != 13 ||
            sigA != 0xED5DCC50 || sigB != 0x91A519B6 || characterClass >= content->characters.size())
            throw ProtocolError("Invalid native TCP/IP logon");
        const HostedGame *room = nullptr;
        for (const auto &[key, candidate] : shared.games) { (void)key; if (candidate.tcpIp) room = &candidate; }
        if (!room || host.participants(room->handle).size() >= room->capacity) throw ProtocolError("TCP/IP host has no available game");
        selectedName = name; tcpIpClass = characterClass; gameName = room->name; gameDifficulty = unsigned(room->settings.difficulty);
        peer.phase = GamePhase::LoggedOn;
        Writer out; out.u8(1); out.u8(uint8_t(gameDifficulty)); out.u32(0); out.u8(1); out.u8(0);
        sendGame(out.release()); sendGame({0}); sendGame({2});
        return;
    }
    if (receivedHash != hash || receivedToken != token || version != 13 || sigA != 0xED5DCC50 || sigB != 0x91A519B6 ||
        name != selectedName || characterClass >= content->characters.size() || content->characters[characterClass].name != selected->player.characterClass)
        throw ProtocolError("D2GS ticket or character mismatch");
    ticket = false; peer.phase = GamePhase::LoggedOn;
    Writer out; out.u8(1); out.u8(uint8_t(gameDifficulty)); out.u16(0); out.u16(0); out.u8(1); out.u8(0); sendGame(out.release()); sendGame({2});

}
void NativeRealmService::queryTcpIpGames(net::protocol::Reader &in) {
    in.finish();
    if (!directTcpIp) throw ProtocolError("TCP/IP game query requires a direct connection");
    for (const auto &[key, room] : shared.games) {
        (void)key; if (!room.tcpIp) continue;
        Writer out; out.u8(0xB2); std::array<uint8_t, 48> name{};
        std::copy(room.name.begin(), room.name.end(), name.begin()); out.append(name);
        out.u16(uint16_t(host.participants(room.handle).size())); out.u16(1); sendGame(out.release());
    }
    Writer out; out.u8(0xB2); out.append(Bytes(48, 0)); out.u16(0); out.u16(0xFFFF); sendGame(out.release());
}
void NativeRealmService::uploadCharacter(net::protocol::Reader &in) {
    if (!directTcpIp || peer.phase != GamePhase::LoggedOn) throw ProtocolError("Unexpected native character upload");
    const auto size = in.u8(); const auto total = in.u32(); const auto part = in.take(size); in.u8(); in.finish();
    // The original encoder appends a final zero-sized part on exact multiples.
    if (tcpIpUploadComplete) {
        if (size || total != tcpIpUploadSize) throw ProtocolError("Duplicate native character upload");
        return;
    }
    const bool first = !tcpIpUpload.incomplete();
    if (auto bytes = tcpIpUpload.append(total, part, first)) {
        auto saved = decodeSave(*bytes, *content);
        if (saved.player.name != selectedName || saved.player.characterClass != content->characters[tcpIpClass].name)
            throw ProtocolError("Uploaded character differs from native logon");
        const auto progression = (bytes->at(0x25) & 31);
        const unsigned unlocked = progression >= 10 ? 2 : progression >= 5 ? 1 : 0;
        if (gameDifficulty > unlocked) throw ProtocolError("Character has not unlocked host difficulty");
        HostedGame *room = nullptr;
        for (auto &[key, candidate] : shared.games) { (void)key; if (candidate.tcpIp && candidate.name == gameName) room = &candidate; }
        if (!room) throw ProtocolError("TCP/IP host game has ended");
        auto folded = [](std::string value) { for (auto &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return value; };
        for (auto *other : shared.peers) if (other != this && other->binding && other->binding->game == room->handle && folded(other->selectedName) == folded(selectedName))
            throw ProtocolError("Character is already in this TCP/IP game");
        admitExternal(std::move(saved), *room, 0, 1);
        ticket = false; tcpIpUploadComplete = true; tcpIpUploadSize = total;
    }
}
void NativeRealmService::enterEnvironment(net::protocol::Reader &in) {
    in.finish();
    if (peer.phase != GamePhase::LoggedOn) throw ProtocolError("Duplicate game admission");
    if (!binding || (directTcpIp && !tcpIpUploadComplete)) throw ProtocolError("Native character upload is incomplete");
    for (auto &packet : admission) sendGame(std::move(packet));
    admission.clear();
    peer.phase = GamePhase::Entered;
    peer.sequence = peer.sentRevision = peer.sentMovement = 0;
    peer.lastMotion.clear();
    peer.areas.insert(RegionId(terrain.request.level));
    if (!host.enter(*binding)) throw ProtocolError("Game admission expired");
    setPaused(false);
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
        if (!binding && !directTcpIp) throw ProtocolError("Native request has no game binding");
        net::protocol::Reader in(bytes); const auto id = ClientMessage(in.u8());
        if (entry.domain == MessageDomain::Lifecycle) {
            switch (id) {
            case ClientMessage::Logon: logon(in); break;
            case ClientMessage::QueryTcpIpGames: queryTcpIpGames(in); break;
            case ClientMessage::UploadCharacter: uploadCharacter(in); break;
            case ClientMessage::EnterEnvironment: enterEnvironment(in); break;
            case ClientMessage::SaveAndLeave: saveAndLeave(in); break;
            case ClientMessage::Ping: ping(in); break;
            default: throw std::logic_error("Lifecycle catalog has no handler");
            }
            in.finish(); ++stats.completed; counters.lastResult = "handled";
            return;
        }
        if (!binding) throw ProtocolError("Native gameplay request has no game binding");
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

#include "native_realm_service.hpp"
#include "hosting/protocol/gameplay_dispatch.hpp"
#include "persistence/save_codec.hpp"
#include "persistence/d2s_header.hpp"
#include "hosting/character_creation.hpp"

namespace d2x::hosting {
using namespace net::protocol;
void NativeRealmService::logon(net::protocol::Reader &in) {
    if (!directTcpIp && (!ticket || !binding || !selected)) throw ProtocolError("Unexpected D2GS logon");
    const auto receivedHash = in.u32(); const auto receivedToken = in.u16(); const auto characterClass = in.u8();
    const auto version = in.u32(); const auto sigA = in.u32(), sigB = in.u32(); in.u8(); const auto nameBytes = in.take(16); in.finish();
    const auto end = std::find(nameBytes.begin(), nameBytes.end(), uint8_t{});
    const std::string name(nameBytes.begin(), end);
    if(version!=13) throw AdmissionFailure(16,"Unsupported native client version");
    if (directTcpIp) {
        if (binding || end == nameBytes.end() || !validCharacterName(name) || receivedToken != 1 || version != 13 ||
            sigA != 0xED5DCC50 || sigB != 0x91A519B6 || characterClass >= content->characters.size())
            throw ProtocolError("Invalid native TCP/IP logon");
        const HostedGame *room = nullptr;
        for (const auto &[key, candidate] : shared.games) { (void)key; if (candidate.tcpIp) room = &candidate; }
        if (!room) throw AdmissionFailure(6,"TCP/IP host has no available game");
        if(host.participants(room->handle).size()>=room->capacity) throw AdmissionFailure(15,"TCP/IP game is full");
        auto folded=[](std::string value){for(auto &c:value) if(c>='A' && c<='Z') c+='a'-'A';return value;};
        for(const auto *other:shared.peers) if(other!=this && other->peer.phase!=GamePhase::Closed && folded(other->selectedName)==folded(name))
            throw AdmissionFailure(6,"Character name is already admitted or reserved");
        selectedName = name; tcpIpClass = characterClass; gameName = room->name; gameDifficulty = unsigned(room->settings.difficulty);
        peer.phase = GamePhase::LoggedOn;
        Writer out; out.u8(1); out.u8(uint8_t(gameDifficulty)); out.u32(room->hardcore?0x800u:0u); out.u8(1); out.u8(0);
        sendGame(out.release()); sendGame({0}); sendGame({2});
        return;
    }
    if (receivedHash != hash || receivedToken != token || version != 13 || sigA != 0xED5DCC50 || sigB != 0x91A519B6 ||
        name != selectedName || characterClass >= content->characters.size() || content->characters[characterClass].name != selected->player.characterClass)
        throw ProtocolError("D2GS ticket or character mismatch");
    ticket = false; peer.phase = GamePhase::LoggedOn;
    Writer out; out.u8(1); out.u8(uint8_t(gameDifficulty)); out.u32(selected->player.hardcore?0x800u:0u); out.u8(1); out.u8(0); sendGame(out.release()); sendGame({2});

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
        PersistentCharacter saved;
        try {saved=decodeSave(*bytes,*content);} catch(const std::exception &) {throw AdmissionFailure(14,"Invalid uploaded native character");}
        const auto header=readD2sHeader(*bytes);
        if (saved.player.name != selectedName || saved.player.characterClass != content->characters[tcpIpClass].name)
            throw ProtocolError("Uploaded character differs from native logon");
        const auto progression = (bytes->at(0x25) & 31);
        const unsigned unlocked = progression >= 10 ? 2 : progression >= 5 ? 1 : 0;
        if (gameDifficulty > unlocked) throw AdmissionFailure(gameDifficulty==1?17:18,"Character has not unlocked host difficulty");
        HostedGame *room = nullptr;
        for (auto &[key, candidate] : shared.games) { (void)key; if (candidate.tcpIp && candidate.name == gameName) room = &candidate; }
        if (!room) throw ProtocolError("TCP/IP host game has ended");
        if(!(header.flags&0x20)) throw AdmissionFailure(23,"Expansion character required");
        if((header.flags&0x0C)==0x0C) throw AdmissionFailure(21,"Hardcore character is dead");
        if(room->hardcore!=bool(header.flags&4)) throw AdmissionFailure(room->hardcore?19:20,"Character hardcore mode does not match game");
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
    if (!host.enter(*binding)) throw AdmissionFailure(6,"Game admission expired");
    const auto rooms=host.visibleRooms(*binding);
    const auto view=host.read(*binding);
    const auto *current=view?terrain.map->activation.room(view->actor.position+Vec{.5f,.5f}):nullptr;
    if (!current || rooms.empty()) throw ProtocolError("Entered player lacks an active native room");
    const server::RoomView own{RegionId(terrain.request.level),
        (int(terrain.origin.x)+current->x)/5,(int(terrain.origin.y)+current->y)/5};
    for (auto &packet : admission) {
        const bool loadAct=!packet.empty() && packet.front()==uint8_t(ServerMessage::LoadAct);
        sendGame(std::move(packet));
        if (loadAct) {
            const auto reveal=[&](const auto &room) {sendGame(encodeServerPacket(ServerMessage::RevealRoom,[&](auto &out) {
                out.u16(uint16_t(room.x));out.u16(uint16_t(room.y));out.u8(uint8_t(room.area));
            }));};
            // Original 1.13c admission reveals the initialized destination,
            // then registers the complete near list (including its root).
            reveal(own);
            for (const auto &room : rooms) reveal(room);
        }
    }
    admission.clear();
    peer.phase = GamePhase::Entered;
    peer.loadPending = true;
    peer.sequence = peer.sentRevision = peer.sentMovement = 0;
    peer.lastMotion.clear();
    peer.areas.insert(RegionId(terrain.request.level));
    peer.rooms.insert(rooms.begin(),rooms.end());
    setPaused(false);
}
void NativeRealmService::saveAndLeave(net::protocol::Reader &in) {
    in.finish();
    leavePending=true;
    finishPendingLeave();
}
void NativeRealmService::finishPendingLeave() {
    if(!leavePending || !binding) return;
    // A prepared live checkpoint must never bypass a subsequent HC death.
    // Wait for its ordinary settlement and persist it before releasing the lease.
    if (reload && selected && selected->player.hardcore) {
        const auto live = host.read(*binding);
        if (!live || live->life <= 0) discardReload();
    }
    const bool preparedReload = reload.has_value();
    if(!preparedReload) {
        if(!host.prepareSave(*binding) || !host.exportCharacter(*binding)) return;
        const auto *room=shared.find(binding->game);
        if(room && room->tcpIp && room->tcpIpOwner==binding->player)
            for(auto *guest:shared.peers) if(guest!=this && guest->binding && guest->binding->game==binding->game)
                if(!host.prepareSave(*guest->binding) || !host.exportCharacter(*guest->binding)) return;
    }
    endGame(!preparedReload, preparedReload);
}
void NativeRealmService::ping(net::protocol::Reader &in) {
    in.u32(); in.u32(); in.u32(); in.finish();
    Bytes pong(33, 0); pong[0] = 0x8F; sendGame(std::move(pong));
}
void NativeRealmService::game(std::span<const uint8_t> bytes) {
    if (bytes.empty()) throw ProtocolError("Empty native client packet");
    if (shared.gamePacketObserver) shared.gamePacketObserver(diagnosticConnection, GamePacketDirection::ClientToServer, bytes);
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
        if(leavePending) {++stats.rejected;counters.lastResult="leaving";return;}
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
    } catch(const AdmissionFailure &error) {
        ++stats.rejected;rejectAdmission(error.code,error.what());
    } catch (const ProtocolError &error) {
        ++stats.malformed;counters.lastResult="malformed";
        if(peer.phase==GamePhase::Connected || peer.phase==GamePhase::LoggedOn) {rejectAdmission(6,error.what());return;}
        throw;
    } catch(const std::exception &error) {
        if(peer.phase==GamePhase::Connected || peer.phase==GamePhase::LoggedOn) {++stats.rejected;rejectAdmission(6,error.what());return;}
        throw;
    }
}
}

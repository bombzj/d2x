#include "pvpgn_server.hpp"
#include "detail/native_realm_service.hpp"
#include "protocol/client_stream.hpp"
#include "network/tcp_stream.hpp"
#include "network/tcp_listener.hpp"
#include "network/protocol/pvpgn.hpp"
#include "persistence/save_codec.hpp"
#include "persistence/d2s_header.hpp"
#include "resources/atomic_file.hpp"
#include "hosting/character_creation.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <ctime>

namespace d2x {
using namespace net::protocol;
using Clock = std::chrono::steady_clock;
namespace {
std::string folded(std::string value) {
    for (auto &letter : value) if (letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
    return value;
}
void identity(std::string_view name) {
    if (name.empty() || name.size() > 15 || std::any_of(name.begin(), name.end(), [](unsigned char letter) {
        return !((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
            (letter >= '0' && letter <= '9') || letter == '-' || letter == '_');
    })) throw ProtocolError("Unsupported backend identity");
}
void store32(Bytes &bytes, size_t offset, uint32_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) throw ProtocolError("Invalid charinfo offset");
    for (unsigned index = 0; index < 4; ++index) bytes[offset + index] = uint8_t(value >> (index * 8));
}
std::string fixedString(const Bytes &bytes, size_t offset, size_t length) {
    if (offset > bytes.size() || length > bytes.size() - offset) throw ProtocolError("Truncated charinfo identity");
    const auto field = std::span<const uint8_t>(bytes).subspan(offset, length);
    const auto end = std::find(field.begin(), field.end(), uint8_t{});
    if (end == field.end()) throw ProtocolError("Unterminated charinfo identity");
    return std::string(field.begin(), end);
}
}
struct PvpgnServer::Impl {
    struct Ticket { uint32_t game{}, token{}; std::string name, account; Clock::time_point expires; };
    struct Peer {
        uint64_t socket{};
        uint32_t game{};
        std::string account, name;
        Bytes portrait;
        hosting::ClientPacketStream input;
        std::unique_ptr<hosting::NativeRealmService> service;
        Clock::time_point deadline = Clock::now() + std::chrono::seconds(30);
        Clock::time_point checkpoint = Clock::now() + std::chrono::seconds(60);
        bool locked{}, entered{}, failed{}, saved{}, leaveNotified{}, leaving{}, disconnected{}, confirmLeave{};
    };
    PvpgnServerOptions options;
    std::function<void(std::string_view)> log;
    hosting::NativeRealmHost host;
    net::TcpStream cs, db;
    net::TcpListener listener;
    PvpgnStream csInput, dbInput;
    std::map<uint64_t, std::unique_ptr<Peer>> peers;
    std::map<std::string, Ticket> tickets;
    std::set<uint32_t> announcedGames;
    uint32_t sequence{}, nextGame{1};
    bool authenticated{}, stopping{}, started{}, databaseFailed{};
    Clock::time_point csDeadline = Clock::now() + std::chrono::seconds(30);
    Impl(Archives &archives, PvpgnServerOptions config, std::function<void(std::string_view)> logger)
        : options(std::move(config)), log(std::move(logger)), host(archives, options.recovery) {}
    uint32_t nextSequence() {
        if (sequence == UINT32_MAX) throw ProtocolError("Backend sequence exhausted; restart required");
        return ++sequence;
    }
    void send(net::TcpStream &stream, uint16_t type, uint32_t serial, Writer body = {}) {
        if (!stream.send(pvpgnFrame(type, serial, body.release()))) throw ProtocolError("Backend send queue unavailable");
    }
    void identities(Writer &out, const Peer &peer) {
        out.string(peer.account, 15); out.string(peer.name, 15); out.string(options.realm, 31);
    }
    void control(const PvpgnPacket &packet) {
        net::protocol::Reader in(packet.body);
        switch (packet.type) {
        case 0x10: {
            if (authenticated) throw ProtocolError("Unexpected D2CS authentication challenge");
            in.u32(); const auto signature = in.u32(); const auto realm = in.string(31);
            if (signature || in.remaining()) throw ProtocolError("Signed D2CS authentication is unsupported");
            if (options.realm.empty()) options.realm = realm;
            if (realm != options.realm || realm.empty()) throw ProtocolError("D2CS realm mismatch");
            Writer reply; reply.u32(options.version); reply.u32(0); reply.u32(0); reply.u32(0); reply.append(Bytes(128, 0));
            send(cs, 0x11, packet.sequence, std::move(reply)); break;
        }
        case 0x11: {
            const auto result = in.u32(); in.finish();
            if (result) throw ProtocolError("D2CS rejected registration; check version/checksum policy");
            authenticated = true; Writer info; info.u32(options.maximumGames); info.u32(0);
            send(cs, 0x12, nextSequence(), std::move(info)); log("D2CS registration accepted"); break;
        }
        case 0x12: in.u32(); in.u32(); in.finish(); break;
        case 0x13: in.finish(); send(cs, 0x13, packet.sequence); break;
        case 0x14: {
            const auto command = in.u32(); in.u32(); in.finish();
            if (command != 1 && command != 2) throw ProtocolError("Unsupported D2CS control command");
            stopping = true; log("D2CS requested graceful shutdown"); break;
        }
        case 0x15:
            in.u32(); in.u32();
            if (in.u32()) throw ProtocolError("D2CS anti-cheat module is unsupported");
            in.string(255); in.string(255); in.finish(); break;
        case 0x16: {
            const auto size = in.u32(); in.u32();
            if (size > in.remaining()) throw ProtocolError("Truncated D2CS configuration");
            in.take(in.remaining()); log("D2CS legacy engine configuration ignored; local D2X configuration retained"); break;
        }
        case 0x20: {
            const auto ladder = in.u8(), expansion = in.u8(), difficulty = in.u8(), hardcore = in.u8();
            auto name = in.string(15), password = in.string(15), description = in.string(31);
            in.string(15); in.string(15); in.string(63); in.finish();
            uint32_t result = 1, id = 0;
            if (authenticated && !stopping && !ladder && expansion == 1 && !hardcore && difficulty <= 2 &&
                !name.empty() && host.games.size() < options.maximumGames && nextGame <= UINT16_MAX && !host.games.contains(folded(name))) {
                hosting::HostedGame room; room.index = nextGame++; id = room.index;
                room.name = std::move(name); room.password = std::move(password); room.description = std::move(description);
                room.capacity = 8; room.levelDifference = 99; room.flags = 4u | uint32_t(difficulty) << 12;
                room.settings = {uint32_t(std::random_device{}()), int(difficulty), false};
                host.games.emplace(folded(room.name), std::move(room)); result = 0;
                announcedGames.insert(id);
                log("D2CS game reserved: " + std::to_string(id));
            }
            Writer reply; reply.u32(result); reply.u32(id); send(cs, 0x20, packet.sequence, std::move(reply)); break;
        }
        case 0x21: {
            const auto game = in.u32(), token = in.u32(); auto name = in.string(15), account = in.string(15);
            in.string(63); in.finish(); identity(name); identity(account);
            const auto room = std::find_if(host.games.begin(), host.games.end(), [&](const auto &entry) { return entry.second.index == game; });
            const auto used = std::count_if(tickets.begin(), tickets.end(), [&](const auto &entry) { return entry.second.game == game; });
            const auto online = std::count_if(peers.begin(), peers.end(), [&](const auto &entry) { return entry.second->game == game; });
            const bool duplicate = tickets.contains(folded(name)) || std::any_of(peers.begin(), peers.end(), [&](const auto &entry) { return folded(entry.second->name) == folded(name); });
            const uint32_t result = !authenticated || stopping || room == host.games.end() || duplicate ? 1 : used + online >= 8 ? 2 : 0;
            if (!result) tickets.emplace(folded(name), Ticket{game, token, name, account, Clock::now() + std::chrono::seconds(30)});
            Writer reply; reply.u32(result); reply.u32(game); send(cs, 0x21, packet.sequence, std::move(reply)); break;
        }
        default: throw ProtocolError("Unsupported D2CS message: " + std::to_string(packet.type));
        }
    }
    void pollControl() {
        for (auto &event : cs.poll()) {
            if (event.kind == net::StreamEventKind::Connected) {
                if (!cs.send({0x64})) throw ProtocolError("Cannot initialize D2CS connection");
            } else if (event.kind == net::StreamEventKind::Data) {
                csDeadline = Clock::now() + std::chrono::seconds(120);
                csInput.append(event.data); PvpgnPacket packet;
                while (csInput.next(packet)) control(packet);
            } else throw ProtocolError("D2CS connection lost");
        }
        if (Clock::now() >= csDeadline) throw ProtocolError("D2CS timeout");
    }
    PvpgnPacket exchange(uint16_t type, Writer body) {
        if (databaseFailed) throw ProtocolError("D2DBS transaction state is unavailable; recovery is required");
        const auto serial = nextSequence(); send(db, type, serial, std::move(body));
        databaseFailed = true;
        const auto deadline = Clock::now() + std::chrono::seconds(15);
        while (Clock::now() < deadline) {
            pollControl();
            for (auto &event : db.poll()) {
                if (event.kind != net::StreamEventKind::Data) throw ProtocolError("D2DBS connection lost during transaction");
                dbInput.append(event.data);
            }
            PvpgnPacket packet;
            while (dbInput.next(packet)) {
                if (packet.type == 0x34) { send(db, 0x34, packet.sequence); continue; }
                if (packet.type != type || packet.sequence != serial) throw ProtocolError("D2DBS response does not match transaction");
                databaseFailed = false;
                return packet;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw ProtocolError("D2DBS transaction timed out; result unknown");
    }
    Bytes load(Peer &peer, uint16_t type) {
        Writer request; request.u16(type); identities(request, peer);
        auto packet = exchange(0x31, std::move(request)); net::protocol::Reader in(packet.body);
        const auto result = in.u32(); in.u32(); in.u32(); const auto receivedType = in.u16(), length = in.u16();
        const auto name = in.string(15);
        if (!result && type == 1) peer.locked = true;
        if (result || receivedType != type || folded(name) != folded(peer.name)) throw ProtocolError("D2DBS character load rejected");
        const auto data = in.take(length); in.finish(); return Bytes(data.begin(), data.end());
    }
    void unlock(Peer &peer) {
        if (!peer.locked) return;
        Writer request; request.u32(0); identities(request, peer); send(db, 0x33, nextSequence(), std::move(request));
        peer.locked = false;
    }
    void saveData(Peer &peer, uint16_t type, const Bytes &data) {
        if (data.size() > 65000) throw ProtocolError("Character exceeds D2DBS packet capacity");
        Writer request; request.u16(type); request.u16(uint16_t(data.size())); identities(request, peer); request.append(data);
        auto packet = exchange(0x30, std::move(request)); net::protocol::Reader in(packet.body);
        const auto result = in.u32(); const auto receivedType = in.u16(); const auto name = in.string(15); in.finish();
        if (result || receivedType != type || folded(name) != folded(peer.name)) throw ProtocolError("D2DBS character save rejected");
    }
    void save(Peer &peer, const PersistentCharacter &character) {
        if (!peer.locked || folded(character.player.name) != folded(peer.name)) throw ProtocolError("D2DBS save has no matching lock");
        const auto bytes = encodeSave(character, *host.content);
        decodeSave(bytes, *host.content);
        const auto recovery = options.recovery / (folded(peer.account) + "-" + folded(peer.name) + ".d2s");
        writeFileAtomically(recovery, bytes, true);
        auto info = peer.portrait;
        if (info.size() != 192) throw ProtocolError("Unsupported D2DBS charinfo layout");
        const auto header = readD2sHeader(bytes);
        const auto appearance = host.portraits->encode(characterAppearance(*host.content, character), header.characterClass, header.level, uint16_t(header.flags));
        if (appearance.size() != 33) throw ProtocolError("Unsupported realm portrait length");
        std::copy(appearance.begin(), appearance.end(), info.begin() + 112);
        info[145] = 0;
        store32(info, 12, uint32_t(std::time(nullptr)));
        store32(info, 176, uint32_t(character.player.experience)); store32(info, 180, header.flags & 0xffffu);
        store32(info, 184, header.level); store32(info, 188, header.characterClass);
        writeFileAtomically(options.recovery / (folded(peer.account) + "-" + folded(peer.name) + ".charinfo"), info, true);
        saveData(peer, 1, bytes); saveData(peer, 2, info);
        peer.portrait = std::move(info); peer.saved = true;
        peer.checkpoint = Clock::now() + std::chrono::seconds(60);
        log("Character save acknowledged: " + peer.name);
    }
    void update(Peer &peer, uint32_t flag) {
        Writer out; out.u32(flag); out.u32(peer.game);
        const auto *character = peer.service && peer.service->selected ? &peer.service->selected->player : nullptr;
        out.u32(character ? uint32_t(character->level) : 0);
        uint32_t classId = 0;
        if (character) for (size_t index = 0; index < host.content->characters.size(); ++index)
            if (host.content->characters[index].name == character->characterClass) classId = uint32_t(index);
        out.u32(classId); out.string(peer.name, 15); send(cs, 0x22, nextSequence(), std::move(out));
    }
    void cleanup(Peer &peer) {
        if (peer.service && peer.service->binding) {
            if (const auto snapshot = host.host.exportCharacter(*peer.service->binding)) peer.service->selected = *snapshot;
            peer.service->checkpoint();
        }
        unlock(peer);
        if (!peer.name.empty() && !peer.leaveNotified) { update(peer, 2); peer.leaveNotified = true; }
        if (peer.service && peer.service->binding) peer.service->close(false);
    }
    void recoverySnapshots() {
        for (auto &[id, peer] : peers) {
            (void)id;
            if (!peer->service || !peer->service->binding) continue;
            try {
                const auto saved = host.host.exportCharacter(*peer->service->binding);
                if (!saved) { log("Recovery snapshot deferred: death settlement is pending for " + peer->name); continue; }
                const auto bytes = encodeSave(*saved, *host.content);
                decodeSave(bytes, *host.content);
                writeFileAtomically(options.recovery / (folded(peer->account) + "-" + folded(peer->name) + ".d2s"), bytes, true);
            } catch (const std::exception &error) { log(std::string("Recovery snapshot failed: ") + error.what()); }
        }
    }
    void closedGames() {
        for (auto game = announcedGames.begin(); game != announcedGames.end();) {
            if (std::any_of(host.games.begin(), host.games.end(), [&](const auto &entry) { return entry.second.index == *game; })) { ++game; continue; }
            Writer out; out.u32(*game); send(cs, 0x23, nextSequence(), std::move(out));
            const auto id = *game;
            std::erase_if(tickets, [&](const auto &entry) { return entry.second.game == id; });
            game = announcedGames.erase(game);
        }
    }
    void admit(Peer &peer, const Bytes &packet) {
        net::protocol::Reader in(packet); if (in.u8() != 0x68) throw ProtocolError("First game packet must be logon");
        const auto token = in.u32(); const auto game = in.u16(); const auto characterClass = in.u8();
        if (in.u32() != 13 || in.u32() != 0xED5DCC50 || in.u32() != 0x91A519B6) throw ProtocolError("Unsupported game client version");
        in.u8(); const auto nameBytes = in.take(16); in.finish();
        const auto end = std::find(nameBytes.begin(), nameBytes.end(), uint8_t{});
        if (end == nameBytes.end()) throw ProtocolError("Unterminated game character name");
        const std::string name(nameBytes.begin(), end);
        const auto ticket = tickets.find(folded(name));
        if (ticket == tickets.end() || ticket->second.expires <= Clock::now() || ticket->second.game != game || ticket->second.token != token)
            throw ProtocolError("Unknown or expired D2CS admission ticket");
        peer.name = ticket->second.name; peer.account = ticket->second.account; peer.game = game; tickets.erase(ticket);
        auto bytes = load(peer, 1); peer.portrait = load(peer, 2);
        if (peer.portrait.size() != 192) throw ProtocolError("Unsupported D2DBS charinfo length");
        net::protocol::Reader info(peer.portrait);
        if (info.u32() != 0x12345678 || info.u32() != 0x10000 ||
            folded(fixedString(peer.portrait, 48, 16)) != folded(peer.name) ||
            folded(fixedString(peer.portrait, 64, 16)) != folded(peer.account) ||
            folded(fixedString(peer.portrait, 80, 32)) != folded(options.realm)) throw ProtocolError("D2DBS charinfo identity mismatch");
        const auto recovery = options.recovery / (folded(peer.account) + "-" + folded(peer.name));
        writeFileAtomically(recovery.string() + ".d2s", bytes, true);
        writeFileAtomically(recovery.string() + ".charinfo", peer.portrait, true);
        // PvPGN's 130-byte v89 template is a first-entry registration record,
        // not an older played character. Do not relax the normal v96 codec.
        net::protocol::Reader registration(bytes);
        const bool newbie = bytes.size() == 130 && registration.u32() == 0xAA55AA55 && registration.u32() == 89;
        PersistentCharacter saved;
        if (newbie) {
            const auto registeredName = fixedString(bytes, 8, 16);
            registration.take(16);
            const auto status = registration.u32();
            registration.take(4);
            const auto marker = registration.u16(), registeredClass = registration.u16();
            net::protocol::Reader summary(std::span<const uint8_t>(peer.portrait).subspan(176));
            const auto experience = summary.u32(), mode = summary.u32(), level = summary.u32(), summaryClass = summary.u32();
            if (folded(registeredName) != folded(name) || status != 0x21 || marker != 0x82 ||
                registeredClass != characterClass || characterClass > 6 || experience || level != 1 ||
                (mode & ~1u) != 0x20 || summaryClass != characterClass)
                throw ProtocolError("Invalid PvPGN new-character registration");
            saved = createCharacter(*host.content, registeredName, characterClass, uint32_t(std::random_device{}()));
            bytes = encodeSave(saved, *host.content);
            log("PvPGN new character initialized from MPQ: " + peer.name);
        } else saved = decodeSave(bytes, *host.content);
        const auto header = readD2sHeader(bytes);
        if (folded(saved.player.name) != folded(name) || header.characterClass != characterClass || !(header.flags & 0x20) || (header.flags & 0x44))
            throw ProtocolError("D2DBS character identity or mode mismatch");
        auto room = std::find_if(host.games.begin(), host.games.end(), [&](const auto &entry) { return entry.second.index == game; });
        if (room == host.games.end()) throw ProtocolError("D2CS game expired during load");
        const auto progression = (header.flags >> 8) & 31;
        if (unsigned(room->second.settings.difficulty) > (progression >= 10 ? 2u : progression >= 5 ? 1u : 0u))
            throw ProtocolError("Character has not unlocked the requested difficulty");
        auto *target = &peer;
        peer.service = std::make_unique<hosting::NativeRealmService>(host,
            [](uint8_t, Bytes) { throw ProtocolError("MCP output is not available on a PvPGN game connection"); },
            [this, target](Bytes output) { if (!listener.send(target->socket, std::move(output))) throw ProtocolError("Game output queue unavailable"); });
        peer.service->externalSave = [this, target](const PersistentCharacter &snapshot) { save(*target, snapshot); };
        peer.service->admitExternal(std::move(saved), room->second, token, game);
        peer.service->connectGame(false);
    }
    void pollGames() {
        for (auto &event : listener.poll()) {
            if (event.stream.kind == net::StreamEventKind::Connected) {
                if (!authenticated || stopping || peers.size() >= 64) { listener.close(event.connection); continue; }
                auto peer = std::make_unique<Peer>(); peer->socket = event.connection;
                peers.emplace(event.connection, std::move(peer)); listener.send(event.connection, {0xAF, 0}); continue;
            }
            const auto found = peers.find(event.connection); if (found == peers.end()) continue;
            auto &peer = *found->second;
            try {
                if (event.stream.kind != net::StreamEventKind::Data) {
                    if (peer.service) peer.service->peer.phase = hosting::GamePhase::Closed;
                    peer.disconnected = true; peer.leaving = true;
                    peer.deadline = Clock::now() + std::chrono::seconds(15); continue;
                }
                if (peer.failed || peer.leaving) continue;
                if (peer.entered) peer.deadline = Clock::now() + std::chrono::seconds(90);
                peer.input.append(event.stream.data); Bytes packet;
                while (peer.input.next(packet)) {
                    if (!peer.service) admit(peer, packet);
                    if (packet[0] == uint8_t(hosting::ClientMessage::SaveAndLeave)) {
                        hosting::requirePhase(hosting::clientMessage(packet[0]), peer.service->peer.phase);
                        peer.leaving = peer.confirmLeave = true;
                        peer.deadline = Clock::now() + std::chrono::seconds(15);
                        peer.service->peer.phase = hosting::GamePhase::Closed; break;
                    }
                    peer.service->game(packet);
                    if (!peer.entered && peer.service->peer.phase == hosting::GamePhase::Entered) { peer.entered = true; peer.deadline = Clock::now() + std::chrono::seconds(90); update(peer, 1); log("Character entered: " + peer.name); }
                    if (peer.service->peer.phase == hosting::GamePhase::Closed) { peer.leaving = true; listener.close(peer.socket); break; }
                }
            } catch (const std::exception &error) {
                peer.failed = peer.leaving = true; listener.close(peer.socket); log(std::string("Game peer failed: ") + error.what());
                if (peer.service) peer.service->peer.phase = hosting::GamePhase::Closed;
                if (!peer.service || !peer.service->binding) { try { unlock(peer); } catch (...) {} }
            }
        }
        for (auto &[id, peer] : peers) {
            if (Clock::now() >= peer->deadline && !peer->failed && !peer->leaving) {
                peer->failed = peer->leaving = true; peer->deadline = Clock::now() + std::chrono::seconds(15);
                if (peer->service) peer->service->peer.phase = hosting::GamePhase::Closed;
                listener.close(id);
            }
            if (peer->service && peer->service->binding && peer->service->peer.phase == hosting::GamePhase::Closed && !peer->failed && !peer->leaving) { peer->failed = peer->leaving = true; listener.close(id); }
        }
    }
    void finishLeaves() {
        for (auto current = peers.begin(); current != peers.end();) {
            auto &peer = *current->second;
            if (!peer.leaving) { ++current; continue; }
            if (peer.service && peer.service->binding && !host.host.exportCharacter(*peer.service->binding)) {
                if (Clock::now() > peer.deadline) throw ProtocolError("Character settlement exceeded leave deadline");
                ++current; continue;
            }
            cleanup(peer);
            if (peer.confirmLeave && !peer.disconnected) {
                if (!listener.send(peer.socket, {0xB0})) throw ProtocolError("Leave confirmation could not be queued");
                listener.closeAfterWrites(peer.socket);
            } else listener.close(peer.socket);
            current = peers.erase(current);
        }
    }
};
PvpgnServer::PvpgnServer(Archives &archives, PvpgnServerOptions options, std::function<void(std::string_view)> log)
    : impl_(std::make_unique<Impl>(archives, std::move(options), std::move(log))) {}
PvpgnServer::~PvpgnServer() = default;
void PvpgnServer::start() {
    auto &state = *impl_;
    if (state.options.maximumGames < 1 || state.options.maximumGames > 256 || state.options.recovery.empty()) throw std::invalid_argument("Invalid server limits or recovery directory");
    std::filesystem::create_directories(state.options.recovery);
    state.host.initialize(); state.listener.listen(state.options.listen);
    state.csDeadline = Clock::now() + std::chrono::seconds(30);
    state.cs.connect(state.options.d2cs, std::chrono::seconds(15)); state.db.connect(state.options.d2dbs, std::chrono::seconds(15));
    const auto deadline = Clock::now() + std::chrono::seconds(15); bool databaseReady = false;
    while (Clock::now() < deadline && (!databaseReady || !state.authenticated)) {
        state.pollControl();
        for (auto &event : state.db.poll()) {
            if (event.kind == net::StreamEventKind::Connected) { if (!state.db.send({0x65})) throw ProtocolError("Cannot initialize D2DBS"); databaseReady = true; }
            else if (event.kind == net::StreamEventKind::Data) state.dbInput.append(event.data);
            else throw ProtocolError("D2DBS connection failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!databaseReady || !state.authenticated) throw ProtocolError("PvPGN startup timed out");
    state.started = true; state.log("Game listener ready on " + state.options.listen.host + ":" + std::to_string(state.options.listen.port));
}
void PvpgnServer::pump(double seconds) {
    auto &state = *impl_; state.pollControl();
    for (auto &event : state.db.poll()) {
        if (event.kind != net::StreamEventKind::Data) throw ProtocolError("D2DBS connection lost");
        state.dbInput.append(event.data);
    }
    PvpgnPacket packet;
    while (state.dbInput.next(packet)) {
        if (packet.type != 0x34) throw ProtocolError("Unsolicited D2DBS reply");
        state.send(state.db, 0x34, packet.sequence);
    }
    state.pollGames(); state.host.advance(seconds); state.finishLeaves();
    if (state.databaseFailed) throw ProtocolError("D2DBS transaction failed; server stopped for recovery");
    for (auto &[id, peer] : state.peers) {
        (void)id;
        if (!peer->service || !peer->service->binding || !peer->entered || Clock::now() < peer->checkpoint) continue;
        if (!state.host.host.exportCharacter(*peer->service->binding)) continue;
        peer->service->checkpoint();
        if (const auto saved = state.host.host.exportCharacter(*peer->service->binding)) peer->service->selected = *saved;
        state.update(*peer, 0);
    }
    std::erase_if(state.tickets, [](const auto &entry) { return entry.second.expires <= Clock::now(); });
    for (auto room = state.host.games.begin(); room != state.host.games.end();) {
        if (!room->second.handle.generation && Clock::now() - room->second.created > std::chrono::seconds(60) &&
            std::none_of(state.tickets.begin(), state.tickets.end(), [&](const auto &entry) { return entry.second.game == room->second.index; })) {
            room = state.host.games.erase(room);
        } else ++room;
    }
    state.closedGames();
}
bool PvpgnServer::stopRequested() const { return impl_->stopping; }
void PvpgnServer::shutdown() {
    auto &state = *impl_; if (!state.started) return;
    state.recoverySnapshots();
    state.stopping = true; Writer capacity; capacity.u32(0); capacity.u32(0); state.send(state.cs, 0x12, state.nextSequence(), std::move(capacity));
    for (auto &[id, peer] : state.peers) { state.listener.close(id); if (peer->service) peer->service->peer.phase = hosting::GamePhase::Closed; }
    const auto settlementDeadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < settlementDeadline && std::any_of(state.peers.begin(), state.peers.end(), [&](const auto &entry) {
        const auto &peer = *entry.second;
        return peer.service && peer.service->binding && !state.host.host.exportCharacter(*peer.service->binding);
    })) { state.pollControl(); state.host.advance(0.04); std::this_thread::sleep_for(std::chrono::milliseconds(40)); }
    std::string failure;
    for (auto &[id, peer] : state.peers) {
        (void)id;
        try { state.cleanup(*peer); }
        catch (const std::exception &error) { failure = error.what(); state.log("Character shutdown save failed: " + peer->name); }
    }
    if (!failure.empty()) { state.recoverySnapshots(); throw ProtocolError("Not all character saves were acknowledged: " + failure); }
    state.peers.clear();
    for (const auto game : state.announcedGames) { Writer out; out.u32(game); state.send(state.cs, 0x23, state.nextSequence(), std::move(out)); }
    const auto deadline = Clock::now() + std::chrono::milliseconds(250);
    while (Clock::now() < deadline) { state.cs.poll(); state.db.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    state.listener.shutdown(); state.cs.close(); state.db.close(); state.started = false; state.log("Shutdown complete; character saves acknowledged");
}
}

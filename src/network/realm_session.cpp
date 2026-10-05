#include "network/realm_session.hpp"
#include "network/protocol/d2gs_stream.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>

namespace d2x::net {
namespace {
using namespace protocol;
using Clock = std::chrono::steady_clock;
bool text_valid(std::string_view value, size_t maximum, bool allowEmpty = false) {
    return (allowEmpty || !value.empty()) && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
bool character_name_valid(std::string_view name) {
    const auto letter = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    return name.size() >= 2 && name.size() <= 15 && letter(name.front()) &&
           std::all_of(name.begin(), name.end(),
                       [&](unsigned char c) { return letter(c) || c == '-' || c == '_'; });
}
std::string ip_address(Reader &in) {
    std::string host;
    for (size_t i = 0; i < 4; ++i) {
        if (i)
            host += '.';
        host += std::to_string(in.u8());
    }
    return host;
}
void finish_zero_padding(Reader &in) {
    while (in.remaining())
        if (in.u8())
            throw ProtocolError("Unexpected packet trailing bytes");
}
OnlineCharacter character_record(Reader &in) {
    OnlineCharacter result;
    result.expiration = in.u32();
    result.name = in.string(15);
    auto portrait = in.string(64);
    result.portrait.assign(portrait.begin(), portrait.end());
    // PvPGN's unsaved creation preview is version 4. Saved native 1.13c
    // previews are version 13 (8D 80), with 14-bit encoded client flags.
    // D2MOO Clients.cpp describes the native layout; do not read a guild
    // emblem byte as Ladder status in the saved layout.
    const auto &bytes = result.portrait;
    const auto version = bytes.size() == 33 ? uint16_t((bytes[0] & 0x7F) | ((bytes[1] & 0x7F) << 7)) : 0;
    if (bytes.size() == 33 && (bytes[0] & 0x80) && (bytes[1] & 0x80) &&
        (version == 4 || version == 10 || version == 13) && bytes[13] >= 1 && bytes[13] <= 7 &&
        bytes[25] >= 1 && bytes[25] <= 99) {
        result.characterClass = uint8_t(result.portrait[13] - 1);
        result.level = result.portrait[25];
        const auto status = uint16_t((bytes[26] & 0x7F) | ((bytes[27] & 0x7F) << 7));
        result.expansion = (status & 0x20) != 0;
        result.hardcore = (status & 0x04) != 0;
        result.dead = (status & 0x08) != 0;
        if (version != 4)
            result.progression = uint8_t((status >> 8) & 0x1F);
        result.ladder = version == 4 ? bytes[30] != 0 && bytes[30] != 0xFF : (status & 0x40) != 0;
    }
    return result;
}
bool pending_stage(OnlineStage stage) {
    switch (stage) {
    case OnlineStage::Idle:
    case OnlineStage::RealmSelection:
    case OnlineStage::CharacterSelection:
    case OnlineStage::Lobby:
    case OnlineStage::ProtocolReady:
    case OnlineStage::Failed:
    case OnlineStage::Cancelled:
        return false;
    default:
        return true;
    }
}
} // namespace
struct RealmSession::Impl {
    TcpStream sid, mcp, gs;
    PacketStream sidPackets{Framing::Sid}, mcpPackets{Framing::Mcp};
    D2gsStream gamePackets;
    OnlineView view;
    LoginOptions options;
    Digest accountHash{}, loginHash{};
    uint32_t clientToken{}, serverToken{}, ticketHash{};
    uint16_t requestCounter{}, pendingRequest{}, lastListRequest{}, ticketToken{};
    std::optional<OnlineCharacter> selected;
    std::string pendingCharacter, gameName, gamePassword;
    std::vector<GamePacket> worldPackets;
    size_t worldBytes{};
    Clock::time_point deadline{}, gameStarted{}, nextHeartbeat{}, lastPing{};
    bool waitingChat{}, environmentSent{}, awaitingPong{}, registering{};
    std::string listFilter;
    ~Impl() { clear_credentials(); }
    void changed() { ++view.revision; }
    void stage(OnlineStage value) {
        view.stage = value;
        deadline = Clock::now() + options.timeout;
        changed();
    }
    void clear_credentials() {
        erase_secret(options.password);
        erase_secret(options.originalClient.classicKey);
        erase_secret(options.originalClient.expansionKey);
        erase_secret(accountHash);
        erase_secret(loginHash);
        erase_secret(gamePassword);
        erase_secret(realmStartup);
        ticketHash = 0;
        ticketToken = 0;
        clientToken = 0;
        serverToken = 0;
    }
    void shutdown() {
        sid.close();
        mcp.close();
        gs.close();
        sidPackets.reset();
        mcpPackets.reset();
        gamePackets.reset();
        worldPackets.clear();
        worldBytes = 0;
        ++view.gameGeneration;
        clear_credentials();
        selected.reset();
        pendingCharacter.clear();
        gameName.clear();
        waitingChat = false;
        environmentSent = false;
        awaitingPong = false;
        registering = false;
        view.gameQueuePosition.reset();
        view.gameListComplete = false;
        listFilter.clear();
    }
    void error(OnlineErrorKind kind, std::string message, uint8_t id = 0, uint32_t code = 0) {
        view.error = OnlineError{kind, id, code, std::move(message)};
        changed();
    }
    void fail(OnlineErrorKind kind, std::string message, uint8_t id = 0, uint32_t code = 0) {
        shutdown();
        view.load = {};
        view.games.clear();
        view.selectedCharacter.clear();
        error(kind, std::move(message), id, code);
        stage(OnlineStage::Failed);
    }
    bool require(OnlineStage value) {
        if (view.stage == value)
            return true;
        error(OnlineErrorKind::Input, "Operation is unavailable in the current connection stage");
        return false;
    }
    void sent(TcpStream &stream, Bytes bytes) {
        if (!stream.send(std::move(bytes)))
            throw ProtocolError("Protocol send queue unavailable");
    }
    void send_sid(uint8_t id, Writer out = {}) { sent(sid, frame(Framing::Sid, id, out.release())); }
    void send_mcp(uint8_t id, Writer out = {}) { sent(mcp, frame(Framing::Mcp, id, out.release())); }
    uint16_t request_id() {
        // IDs are real MCP wire IDs; no automatic wrap/reuse within one login.
        if (requestCounter == std::numeric_limits<uint16_t>::max())
            throw ProtocolError("MCP request IDs exhausted; reconnect required");
        pendingRequest = ++requestCounter;
        return pendingRequest;
    }
    void expect(OnlineStage value) const {
        if (view.stage != value)
            throw ProtocolError("Protocol reply arrived in an unexpected stage");
    }
    void request_realms() {
        send_sid(0x40);
        stage(OnlineStage::ListingRealms);
    }
    void request_logon() {
        Writer out;
        out.u32(clientToken);
        out.u32(serverToken);
        out.append(loginHash);
        out.string(options.account, 32);
        send_sid(0x3A, std::move(out));
        stage(OnlineStage::LoggingIn);
    }
    void request_characters() {
        Writer out;
        out.u32(18);
        send_mcp(0x19, std::move(out));
        stage(OnlineStage::ListingCharacters);
    }
    void start_account() {
        sent(sid, {1});
        Writer out;
        out.u32(0);
        out.u32(0x49583836);
        out.u32(0x44325850);
        out.u32(0x0D);
        out.u32(options.languageCode);
        out.u32(0);
        out.u32(uint32_t(options.timeZoneBiasMinutes));
        out.u32(options.localeId);
        out.u32(options.localeId);
        out.string(options.countryCode, 3);
        out.string(options.countryName, 64);
        send_sid(0x50, std::move(out));
        stage(OnlineStage::AuthChallenge);
    }
    void handle_sid(Packet packet) {
        Reader in(packet.body);
        if (packet.id == 0x25) {
            const auto ping = in.u32();
            in.finish();
            Writer out;
            out.u32(ping);
            send_sid(0x25, std::move(out));
            return;
        }
        if (packet.id == 0x4C) {
            fail(OnlineErrorKind::Unsupported, "Server requires an unsupported authentication module",
                 packet.id);
            return;
        }
        switch (packet.id) {
        case 0x50: {
            expect(OnlineStage::AuthChallenge);
            AuthChallenge challenge;
            challenge.logonType = in.u32();
            challenge.serverToken = in.u32();
            challenge.udpToken = in.u32();
            challenge.fileTime = in.u64();
            challenge.revisionFile = in.string(128);
            challenge.formula = in.string(256);
            in.finish();
            serverToken = challenge.serverToken;
            auto proof = prepare_auth(challenge, clientToken, options.originalClient);
            loginHash = logon_hash(clientToken, serverToken, accountHash);
            if (!registering)
                erase_secret(accountHash);
            erase_secret(options.originalClient.classicKey);
            erase_secret(options.originalClient.expansionKey);
            Writer out;
            out.u32(clientToken);
            out.u32(proof.executableVersion);
            out.u32(proof.executableHash);
            out.u32(uint32_t(proof.keys.size()));
            out.u32(0);
            for (auto &key : proof.keys) {
                out.u32(key.length);
                out.u32(key.product);
                out.u32(key.publicValue);
                out.u32(0);
                out.append(key.hash);
                erase_secret(key.hash);
            }
            out.string(proof.executableInfo, 1024);
            out.string(proof.owner, 64);
            send_sid(0x51, std::move(out));
            stage(OnlineStage::AuthenticatingVersion);
            break;
        }
        case 0x51: {
            expect(OnlineStage::AuthenticatingVersion);
            const auto result = in.u32();
            in.string(1024);
            finish_zero_padding(in);
            if (result) {
                fail(OnlineErrorKind::Authentication, "Server rejected client version or key proof",
                     packet.id, result);
                return;
            }
            if (registering) {
                Writer out;
                out.append(accountHash);
                out.string(options.account, 15);
                send_sid(0x3D, std::move(out));
                erase_secret(accountHash);
                stage(OnlineStage::CreatingAccount);
            } else
                request_logon();
            break;
        }
        case 0x3D: {
            expect(OnlineStage::CreatingAccount);
            const auto result = in.u32();
            if (in.remaining())
                in.string(1024);
            finish_zero_padding(in);
            if (result) {
                fail(OnlineErrorKind::Authentication, "Account creation was rejected", packet.id, result);
                return;
            }
            registering = false;
            request_logon();
            break;
        }
        case 0x3A: {
            expect(OnlineStage::LoggingIn);
            const auto result = in.u32();
            // Banned-account replies may append a reason. It is never copied to logs/view.
            if (in.remaining())
                in.string(1024);
            finish_zero_padding(in);
            if (result) {
                fail(OnlineErrorKind::Authentication, "Account login was rejected", packet.id, result);
                return;
            }
            request_realms();
            break;
        }
        case 0x40: {
            expect(OnlineStage::ListingRealms);
            in.u32();
            const auto count = in.u32();
            if (count > 64)
                throw ProtocolError("Realm list limit exceeded");
            std::vector<OnlineRealm> realms;
            for (uint32_t i = 0; i < count; ++i) {
                in.u32();
                realms.push_back({in.string(64), in.string(256)});
            }
            in.finish();
            view.realms = std::move(realms);
            stage(OnlineStage::RealmSelection);
            break;
        }
        case 0x3E: {
            expect(OnlineStage::ConnectingRealm);
            const auto cookie = in.u32(), status = in.u32();
            if (status) {
                fail(OnlineErrorKind::Server, "Realm logon was rejected", packet.id, status);
                return;
            }
            const auto first = in.take(8);
            const auto host = ip_address(in);
            const auto port = in.be16();
            in.take(2);
            const auto second = in.take(48);
            const auto uniqueName = in.string(64);
            in.finish();
            if (!port || host == "0.0.0.0")
                throw ProtocolError("Realm returned an invalid endpoint");
            Writer startup;
            startup.u32(cookie);
            startup.u32(status);
            startup.append(first);
            startup.append(second);
            startup.string(uniqueName, 64);
            realmStartup = startup.release();
            mcpPackets.reset();
            mcp.connect({host, port}, options.timeout);
            break;
        }
        case 0x0A: {
            if (view.stage != OnlineStage::SelectingCharacter || !waitingChat)
                break;
            in.string(64);
            in.string(256);
            in.string(32);
            in.finish();
            waitingChat = false;
            Writer out;
            out.string(pendingCharacter, 15);
            send_mcp(0x07, std::move(out));
            deadline = Clock::now() + options.timeout;
            break;
        }
        default:
            break; // SID has a length header; unsupported noncritical payloads stay opaque.
        }
    }
    Bytes realmStartup;
    void join_requested_game() {
        Writer out;
        out.u16(request_id());
        out.string(gameName, 15);
        out.string(gamePassword, 15);
        send_mcp(0x04, std::move(out));
        stage(OnlineStage::JoiningGame);
    }
    void handle_mcp(Packet packet) {
        Reader in(packet.body);
        switch (packet.id) {
        case 0x01: {
            expect(OnlineStage::RealmStartup);
            const auto result = in.u32();
            in.finish();
            if (result) {
                fail(OnlineErrorKind::Server, "Realm startup was rejected", packet.id, result);
                return;
            }
            request_characters();
            break;
        }
        case 0x19: {
            expect(OnlineStage::ListingCharacters);
            const auto requested = in.u16();
            in.u32();
            const auto count = in.u16();
            if (count > 18 || count > requested)
                throw ProtocolError("Character list limit exceeded");
            std::vector<OnlineCharacter> characters;
            for (uint16_t i = 0; i < count; ++i)
                characters.push_back(character_record(in));
            in.finish();
            view.characters = std::move(characters);
            stage(OnlineStage::CharacterSelection);
            break;
        }
        case 0x07: {
            expect(OnlineStage::SelectingCharacter);
            if (waitingChat)
                throw ProtocolError("Character logon reply preceded chat startup");
            const auto result = in.u32();
            in.finish();
            if (result) {
                error(OnlineErrorKind::Server, "Character selection was rejected", packet.id, result);
                selected.reset();
                pendingCharacter.clear();
                stage(OnlineStage::CharacterSelection);
                return;
            }
            view.selectedCharacter = pendingCharacter;
            pendingCharacter.clear();
            stage(OnlineStage::Lobby);
            break;
        }
        case 0x02: {
            expect(OnlineStage::CreatingCharacter);
            const auto result = in.u32();
            in.finish();
            if (result) {
                error(OnlineErrorKind::Server, "Character creation was rejected", packet.id, result);
                stage(OnlineStage::CharacterSelection);
            } else
                request_characters();
            break;
        }
        case 0x0A: {
            expect(OnlineStage::DeletingCharacter);
            in.u16();
            const auto result = in.u32();
            in.finish();
            if (result) {
                error(OnlineErrorKind::Server, "Character deletion was rejected", packet.id, result);
                stage(OnlineStage::CharacterSelection);
            } else
                request_characters();
            break;
        }
        case 0x03: {
            expect(OnlineStage::CreatingGame);
            if (in.u16() != pendingRequest)
                throw ProtocolError("MCP create-game request ID mismatch");
            in.u16();
            in.u16();
            const auto result = in.u32();
            in.finish();
            if (result) {
                game_rejected(packet.id, result, "Game creation was rejected");
                return;
            }
            view.gameQueuePosition.reset();
            join_requested_game();
            break;
        }
        case 0x04: {
            expect(OnlineStage::JoiningGame);
            if (in.u16() != pendingRequest)
                throw ProtocolError("MCP join-game request ID mismatch");
            const auto token = in.u16();
            in.u16();
            const auto host = ip_address(in);
            const auto hash = in.u32(), result = in.u32();
            in.finish();
            if (result) {
                game_rejected(packet.id, result, "Game joining was rejected");
                return;
            }
            if (host == "0.0.0.0")
                throw ProtocolError("Game server returned an invalid endpoint");
            ticketToken = token;
            ticketHash = hash;
            erase_secret(gamePassword);
            gameName.clear();
            // Original Realm clients terminate MCP before opening the game connection.
            mcp.close();
            mcpPackets.reset();
            ++view.gameGeneration;
            view.load = {};
            worldPackets.clear();
            worldBytes = 0;
            environmentSent = false;
            awaitingPong = false;
            gamePackets.reset();
            gs.connect({host, options.gamePort}, options.timeout);
            stage(OnlineStage::ConnectingGame);
            break;
        }
        case 0x05: {
            const auto request = in.u16();
            // List requests can time out while MCP stays usable. A late framed
            // reply must not affect the next operation or be treated as its result.
            if (request && request <= lastListRequest &&
                (view.stage != OnlineStage::ListingGames || request != pendingRequest))
                return;
            expect(OnlineStage::ListingGames);
            if (request != pendingRequest)
                throw ProtocolError("MCP game-list request ID mismatch");
            OnlineGame game;
            game.index = in.u32();
            game.players = in.u8();
            game.flags = in.u32();
            game.name = in.string(15);
            game.description = in.string(256);
            finish_zero_padding(in);
            if (!game.index) {
                view.gameListComplete = true;
                stage(OnlineStage::Lobby);
                return;
            }
            if (view.games.size() >= 256)
                throw ProtocolError("Game list limit exceeded");
            if (!(game.flags & 0x00200000) &&
                (listFilter.empty() || game.name.find(listFilter) != std::string::npos))
                view.games.push_back(std::move(game));
            changed();
            break;
        }
        case 0x14: {
            if (view.stage != OnlineStage::CreatingGame)
                throw ProtocolError("Unexpected game creation queue");
            view.gameQueuePosition = in.u32();
            in.finish();
            changed();
            deadline = Clock::now() + options.timeout;
            break;
        }
        default:
            break;
        }
    }
    void game_rejected(uint8_t id, uint32_t code, std::string message) {
        erase_secret(gamePassword);
        gameName.clear();
        view.gameQueuePosition.reset();
        error(OnlineErrorKind::Server, std::move(message), id, code);
        stage(OnlineStage::Lobby);
    }
    bool choose_realm(std::string name) {
        if (!require(OnlineStage::RealmSelection))
            return false;
        if (std::none_of(view.realms.begin(), view.realms.end(),
                         [&](const auto &realm) { return realm.name == name; })) {
            error(OnlineErrorKind::Input, "Choose a Realm returned by the server");
            return false;
        }
        Writer out;
        out.u32(clientToken);
        out.append(loginHash);
        out.string(name, 64);
        send_sid(0x3E, std::move(out));
        view.error.reset();
        view.selectedRealm = std::move(name);
        view.characters.clear();
        stage(OnlineStage::ConnectingRealm);
        return true;
    }
    void return_to_realm() {
        gs.close();
        gamePackets.reset();
        worldPackets.clear();
        worldBytes = 0;
        ++view.gameGeneration;
        view.load = {};
        view.games.clear();
        view.gameListComplete = false;
        view.gameQueuePosition.reset();
        view.selectedCharacter.clear();
        selected.reset();
        auto realm = view.selectedRealm;
        stage(OnlineStage::RealmSelection);
        choose_realm(std::move(realm));
    }
    void handle_game(Packet packet) {
        if (packet.id == 0xAF && view.stage == OnlineStage::GameHandshake) {
            if (!selected || !selected->characterClass)
                throw ProtocolError("Missing game character class");
            sent(gs, game_logon(ticketHash, ticketToken, *selected->characterClass, options.gameLocale,
                                selected->name));
            ticketHash = 0;
            ticketToken = 0;
            gameStarted = Clock::now();
            nextHeartbeat = gameStarted;
            stage(OnlineStage::LoadingGame);
            return;
        }
        if (packet.id == 0xB0 || packet.id == 0x06) {
            if (view.stage == OnlineStage::LeavingGame)
                return_to_realm();
            else
                fail(OnlineErrorKind::Server, "Game server ended the game connection", packet.id);
            return;
        }
        if (packet.id == 0x8F) {
            if (awaitingPong) {
                view.latencyMilliseconds = uint32_t(
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastPing).count());
                awaitingPong = false;
                changed();
            }
            return;
        }
        Reader in(packet.body);
        if (packet.id == 0x01) {
            view.load.difficulty = in.u8();
            in.u16();
            in.u16();
            const auto expansion = in.u8(), ladder = in.u8();
            in.finish();
            if (!expansion || ladder) {
                fail(OnlineErrorKind::Unsupported, "Only non-Ladder LoD games are supported", packet.id);
                return;
            }
            changed();
        } else if (packet.id == 0x02 && !environmentSent) {
            // Server has loaded the character. This sends no local D2S or mod packet.
            sent(gs, {0x6B});
            environmentSent = true;
            deadline = Clock::now() + options.timeout;
        } else if (packet.id == 0x03) {
            view.load.act = in.u8();
            view.load.mapSeed = in.u32();
            view.load.townArea = in.u16();
            // Preserve the secondary value without claiming cross-version DRLG semantics.
            view.load.secondarySeed = in.u32();
            in.finish();
            changed();
        } else if (packet.id == 0x04) {
            view.load.serverLoadComplete = true;
            changed();
        } else if (packet.id == 0x0B) {
            const auto type = in.u8();
            const auto id = in.u32();
            in.finish();
            if (type == 0) {
                view.load.playerUnitId = id;
                changed();
            }
        }
        if (worldPackets.size() >= 4096 || packet.body.size() + 1 > 2 * 1024 * 1024 - worldBytes)
            throw ProtocolError("World packet queue limit exceeded; drain packets regularly");
        worldBytes += packet.body.size() + 1;
        worldPackets.push_back({view.gameGeneration, std::move(packet)});
        if (view.stage == OnlineStage::LoadingGame && environmentSent && view.load.serverLoadComplete &&
            view.load.playerUnitId && view.load.mapSeed)
            stage(OnlineStage::ProtocolReady);
    }
    void pump_sid() {
        for (auto &event : sid.poll()) {
            if (event.kind == StreamEventKind::Connected) {
                expect(OnlineStage::ConnectingAccount);
                start_account();
            } else if (event.kind == StreamEventKind::Data) {
                sidPackets.append(event.data);
                Packet packet;
                while (sidPackets.next(packet)) {
                    handle_sid(std::move(packet));
                    if (view.stage == OnlineStage::Failed)
                        return;
                }
            } else {
                if (!sidPackets.empty())
                    throw ProtocolError("Account stream ended inside a packet");
                fail(OnlineErrorKind::Transport, "Account connection closed or failed");
                return;
            }
        }
    }
    void pump_mcp() {
        for (auto &event : mcp.poll()) {
            if (event.kind == StreamEventKind::Connected) {
                expect(OnlineStage::ConnectingRealm);
                sent(mcp, {1});
                sent(mcp, frame(Framing::Mcp, 0x01, realmStartup));
                erase_secret(realmStartup);
                stage(OnlineStage::RealmStartup);
            } else if (event.kind == StreamEventKind::Data) {
                mcpPackets.append(event.data);
                Packet packet;
                while (mcpPackets.next(packet)) {
                    handle_mcp(std::move(packet));
                    if (view.stage == OnlineStage::Failed || view.stage == OnlineStage::ConnectingGame)
                        return;
                }
            } else {
                if (!mcpPackets.empty())
                    throw ProtocolError("Realm stream ended inside a packet");
                fail(OnlineErrorKind::Transport, "Realm connection closed or failed");
                return;
            }
        }
    }
    void pump_game() {
        for (auto &event : gs.poll()) {
            if (event.kind == StreamEventKind::Connected) {
                expect(OnlineStage::ConnectingGame);
                stage(OnlineStage::GameHandshake);
            } else if (event.kind == StreamEventKind::Data) {
                gamePackets.append(event.data);
                Packet packet;
                while (gamePackets.next(packet)) {
                    handle_game(std::move(packet));
                    if (view.stage == OnlineStage::Failed || view.stage == OnlineStage::ConnectingRealm)
                        return;
                }
            } else if (event.kind == StreamEventKind::Closed && view.stage == OnlineStage::LeavingGame &&
                       gamePackets.empty()) {
                return_to_realm();
                return;
            } else {
                if (!gamePackets.empty())
                    throw ProtocolError("Game stream ended inside a packet or compression envelope");
                fail(OnlineErrorKind::Transport, "Game connection closed or failed");
                return;
            }
        }
    }
    void tick() {
        pump_sid();
        if (view.stage == OnlineStage::Failed)
            return;
        pump_mcp();
        if (view.stage == OnlineStage::Failed)
            return;
        pump_game();
        if (view.stage == OnlineStage::Failed)
            return;
        const auto now = Clock::now();
        if (pending_stage(view.stage) && now >= deadline) {
            if (view.stage == OnlineStage::ListingGames) {
                view.games.clear();
                error(OnlineErrorKind::Timeout, "Game list did not finish before timeout");
                stage(OnlineStage::Lobby);
            } else
                fail(OnlineErrorKind::Timeout, "Connection stage timed out");
            return;
        }
        if ((view.stage == OnlineStage::LoadingGame || view.stage == OnlineStage::ProtocolReady) &&
            now >= nextHeartbeat) {
            if (awaitingPong && now - lastPing >= options.timeout) {
                fail(OnlineErrorKind::Timeout, "Game heartbeat timed out");
                return;
            }
            // One outstanding ping: the 1.13c response is not a request-ID acknowledgement.
            if (!awaitingPong) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - gameStarted).count();
                sent(gs, game_ping(uint32_t(elapsed), view.latencyMilliseconds));
                lastPing = now;
                awaitingPong = true;
            }
            nextHeartbeat = now + options.heartbeat;
        }
    }
};
RealmSession::RealmSession() : impl_(std::make_unique<Impl>()) {}
RealmSession::~RealmSession() = default;
void RealmSession::login(LoginOptions options) {
    authenticate(std::move(options), false);
}
void RealmSession::register_account(LoginOptions options) {
    authenticate(std::move(options), true);
}
void RealmSession::authenticate(LoginOptions options, bool createAccount) {
    auto &p = *impl_;
    p.shutdown();
    const auto revision = p.view.revision, generation = p.view.connectionGeneration,
               gameGeneration = p.view.gameGeneration;
    p.view = {};
    p.view.revision = revision;
    p.view.connectionGeneration = generation + 1;
    p.view.gameGeneration = gameGeneration;
    p.requestCounter = 0;
    p.lastListRequest = 0;
    p.registering = createAccount;
    try {
        if (!text_valid(options.account, 32) || !options.accountServer.port ||
            options.accountServer.host.empty() || options.timeout.count() <= 0 ||
            options.heartbeat.count() <= 0 || options.heartbeat >= options.timeout || !options.gamePort ||
            !text_valid(options.countryCode, 3) || !text_valid(options.countryName, 64))
            throw ProtocolError("Invalid login options");
        if (createAccount && (!text_valid(options.account, 15) || options.account.size() < 2 ||
                              !text_valid(options.password, 15) || options.password.size() < 2))
            throw ProtocolError("Invalid account creation fields");
        p.accountHash = password_hash(options.password);
        erase_secret(options.password);
        p.options = std::move(options);
        std::random_device random;
        p.clientToken = uint32_t(random());
        if (!p.clientToken)
            p.clientToken = uint32_t(random());
        p.sid.connect(p.options.accountServer, p.options.timeout);
        p.stage(OnlineStage::ConnectingAccount);
    } catch (const std::exception &) {
        erase_secret(options.password);
        erase_secret(options.originalClient.classicKey);
        erase_secret(options.originalClient.expansionKey);
        p.fail(OnlineErrorKind::Input, "Login configuration could not be prepared");
    }
}
bool RealmSession::choose_realm(std::string name) {
    try {
        return impl_->choose_realm(std::move(name));
    } catch (const std::exception &) {
        impl_->fail(OnlineErrorKind::Protocol, "Realm request could not be encoded");
        return false;
    }
}
bool RealmSession::return_to_realms() {
    auto &p = *impl_;
    if (!p.require(OnlineStage::CharacterSelection))
        return false;
    try {
        p.mcp.close();
        p.mcpPackets.reset();
        p.view.characters.clear();
        p.view.selectedRealm.clear();
        p.view.selectedCharacter.clear();
        p.selected.reset();
        p.view.error.reset();
        p.request_realms();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Realm list could not be reopened");
        return false;
    }
}
bool RealmSession::create_character(CreateCharacterOptions options) {
    auto &p = *impl_;
    if (!p.require(OnlineStage::CharacterSelection))
        return false;
    if (!character_name_valid(options.name) || options.characterClass > 6 || p.view.characters.size() >= 18) {
        p.error(OnlineErrorKind::Input,
                "Use a 2-15 letter character name (hyphen/underscore allowed), and a LoD class");
        return false;
    }
    try {
        Writer out;
        out.u16(options.characterClass);
        out.u16(0);
        out.u16(0x20 | (options.hardcore ? 0x04 : 0));
        out.string(options.name, 15);
        p.view.error.reset();
        p.send_mcp(0x02, std::move(out));
        p.stage(OnlineStage::CreatingCharacter);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Protocol, "Character creation request could not be encoded");
        return false;
    }
}
bool RealmSession::delete_character(std::string name) {
    auto &p = *impl_;
    if (!p.require(OnlineStage::CharacterSelection))
        return false;
    if (!text_valid(name, 15) || std::none_of(p.view.characters.begin(), p.view.characters.end(),
                                              [&](const auto &c) { return c.name == name; })) {
        p.error(OnlineErrorKind::Input, "Select a character returned by this Realm before deleting it");
        return false;
    }
    try {
        Writer out;
        out.u16(0);
        out.string(name, 15);
        p.view.error.reset();
        p.send_mcp(0x0A, std::move(out));
        p.stage(OnlineStage::DeletingCharacter);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Protocol, "Character deletion request could not be encoded");
        return false;
    }
}
bool RealmSession::select_character(std::string name) {
    auto &p = *impl_;
    if (!p.require(OnlineStage::CharacterSelection))
        return false;
    auto found = std::find_if(p.view.characters.begin(), p.view.characters.end(),
                              [&](const auto &character) { return character.name == name; });
    if (found == p.view.characters.end() || !text_valid(name, 15) || !found->characterClass ||
        !found->expansion.value_or(false) || found->ladder.value_or(true) ||
        (found->hardcore.value_or(true) && found->dead.value_or(true))) {
        p.error(OnlineErrorKind::Unsupported, "Select a known, playable, non-Ladder LoD character");
        return false;
    }
    try {
        p.selected = *found;
        p.pendingCharacter = std::move(name);
        p.view.error.reset();
        Writer out;
        out.string(p.pendingCharacter, 15);
        // PvPGN PLAYERINFOREQ for LoD expects Realmname,charname, not a bare Realm.
        out.string(p.view.selectedRealm + ',' + p.pendingCharacter, 80);
        p.send_sid(0x0A, std::move(out));
        p.waitingChat = true;
        p.stage(OnlineStage::SelectingCharacter);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Protocol, "Character request could not be encoded");
        return false;
    }
}
bool RealmSession::list_games(std::string filter) {
    auto &p = *impl_;
    if (!p.require(OnlineStage::Lobby))
        return false;
    if (!text_valid(filter, 15, true)) {
        p.error(OnlineErrorKind::Input, "Invalid game list filter");
        return false;
    }
    try {
        Writer out;
        out.u16(p.request_id());
        out.u32(p.selected && p.selected->hardcore.value_or(false) ? 0x800 : 0);
        p.listFilter = std::move(filter); // MCP filter is local: the reference server ignores extra bytes.
        p.lastListRequest = p.pendingRequest;
        p.send_mcp(0x05, std::move(out));
        p.view.games.clear();
        p.view.gameListComplete = false;
        p.view.error.reset();
        p.stage(OnlineStage::ListingGames);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Protocol, "Game list request could not be encoded");
        return false;
    }
}
bool RealmSession::cancel_game_list() {
    auto &p = *impl_;
    if (!p.require(OnlineStage::ListingGames))
        return false;
    p.view.games.clear();
    p.view.gameListComplete = false;
    p.view.error.reset();
    p.stage(OnlineStage::Lobby);
    return true;
}
bool RealmSession::create_game(CreateGameOptions options) {
    auto &p = *impl_;
    if (!p.require(OnlineStage::Lobby)) {
        erase_secret(options.password);
        return false;
    }
    if (!text_valid(options.name, 15) || !text_valid(options.password, 15, true) ||
        !text_valid(options.description, 31, true) || options.difficulty > 2 || !options.maximumPlayers ||
        options.maximumPlayers > 8 || options.levelDifference > 99) {
        erase_secret(options.password);
        p.error(OnlineErrorKind::Input, "Invalid game creation options");
        return false;
    }
    const auto progression = p.selected ? p.selected->progression.value_or(0) : 0;
    const uint8_t unlocked = progression >= 10 ? 2 : progression >= 5 ? 1 : 0;
    if (options.difficulty > unlocked) {
        erase_secret(options.password);
        p.error(OnlineErrorKind::Input, "The selected character has not unlocked this difficulty");
        return false;
    }
    try {
        Writer out;
        out.u16(p.request_id());
        out.u32(0x04 | uint32_t(options.difficulty) << 12);
        out.u8(1);
        out.u8(options.levelDifference);
        out.u8(options.maximumPlayers);
        out.string(options.name, 15);
        out.string(options.password, 15);
        out.string(options.description, 31);
        p.send_mcp(0x03, std::move(out));
        p.gameName = std::move(options.name);
        p.gamePassword = std::move(options.password);
        p.view.gameQueuePosition.reset();
        p.view.error.reset();
        p.stage(OnlineStage::CreatingGame);
        return true;
    } catch (const std::exception &) {
        erase_secret(options.password);
        p.fail(OnlineErrorKind::Protocol, "Game creation request could not be encoded");
        return false;
    }
}
bool RealmSession::join_game(std::string name, std::string password) {
    auto &p = *impl_;
    if (!p.require(OnlineStage::Lobby)) {
        erase_secret(password);
        return false;
    }
    if (!text_valid(name, 15) || !text_valid(password, 15, true)) {
        erase_secret(password);
        p.error(OnlineErrorKind::Input, "Invalid game joining options");
        return false;
    }
    try {
        p.gameName = std::move(name);
        p.gamePassword = std::move(password);
        p.view.error.reset();
        p.join_requested_game();
        return true;
    } catch (const std::exception &) {
        erase_secret(password);
        p.fail(OnlineErrorKind::Protocol, "Game join request could not be encoded");
        return false;
    }
}
bool RealmSession::leave_game() {
    auto &p = *impl_;
    if (p.view.stage != OnlineStage::ProtocolReady && p.view.stage != OnlineStage::LoadingGame) {
        p.error(OnlineErrorKind::Input, "No game is available to leave");
        return false;
    }
    try {
        p.sent(p.gs, {0x69});
        p.view.error.reset();
        p.stage(OnlineStage::LeavingGame);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Game leave request could not be queued");
        return false;
    }
}
bool RealmSession::return_to_characters() {
    auto &p = *impl_;
    if (!p.require(OnlineStage::Lobby))
        return false;
    try {
        p.mcp.close();
        p.mcpPackets.reset();
        p.return_to_realm();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Realm character list could not be reopened");
        return false;
    }
}
void RealmSession::tick() {
    try {
        impl_->tick();
    } catch (const ProtocolError &error) {
        impl_->fail(OnlineErrorKind::Protocol, error.what());
    } catch (const std::exception &) {
        impl_->fail(OnlineErrorKind::Protocol, "Network processing failed");
    }
}
void RealmSession::cancel() {
    impl_->shutdown();
    impl_->view.load = {};
    impl_->view.error.reset();
    ++impl_->view.connectionGeneration;
    impl_->view.games.clear();
    impl_->view.selectedCharacter.clear();
    impl_->stage(OnlineStage::Cancelled);
}
void RealmSession::logout() {
    auto &p = *impl_;
    p.shutdown();
    const auto revision = p.view.revision, generation = p.view.connectionGeneration,
               gameGeneration = p.view.gameGeneration;
    p.view = {};
    p.view.revision = revision + 1;
    p.view.connectionGeneration = generation + 1;
    p.view.gameGeneration = gameGeneration;
}
const OnlineView &RealmSession::read() const {
    return impl_->view;
}
std::vector<GamePacket> RealmSession::take_game_packets() {
    auto packets = std::move(impl_->worldPackets);
    impl_->worldPackets.clear();
    impl_->worldBytes = 0;
    return packets;
}
} // namespace d2x::net

#include "network/realm_session.hpp"
#include "network/tcp_stream.hpp"
#include "client/remote_world.hpp"
#include "network/protocol/d2gs_stream.hpp"
#include "network/protocol/bits.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>

namespace d2x::net {
namespace {
using namespace protocol;
using Clock = std::chrono::steady_clock;
Bytes preauthenticatedRealmStartup() {
    Writer out;
    out.append(Bytes(64, 0)); // Cookie, status, native 8-byte and 48-byte chunks.
    out.string("SinglePlayer", 64);
    return out.release();
}
bool text_valid(std::string_view value, size_t maximum, bool allowEmpty = false) {
    return (allowEmpty || !value.empty()) && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
bool character_name_valid(std::string_view name) {
    const auto letter = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    return !name.empty() && name.size() <= 15 &&
           std::all_of(name.begin(), name.end(),
                       [&](unsigned char c) { return letter(c) || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
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
    // Calls and background service are serialized. Nested native operations (for
    // example Hardcore respawn -> leave) reuse this same lock.
    std::recursive_mutex mutex;
    std::condition_variable_any wake;
    bool retainGamePackets{}, snapshotDirty{};
    std::jthread worker;

    ByteStream sid, mcp, gs;
    std::optional<Endpoint> directRealm;
    PacketStream sidPackets{Framing::Sid}, mcpPackets{Framing::Mcp};
    D2gsStream gamePackets;
    OnlineView view;
    LoginOptions options;
    Digest accountHash{}, loginHash{};
    uint32_t clientToken{}, serverToken{}, ticketHash{};
    uint16_t requestCounter{}, pendingRequest{}, lastListRequest{}, ticketToken{};
    uint16_t gameInfoRequest{};
    Clock::time_point gameInfoDeadline{};
    std::optional<OnlineCharacter> selected;
    std::string pendingCharacter, gameName, gamePassword;
    std::vector<GamePacket> worldPackets;
    size_t worldBytes{};
    Clock::time_point deadline{}, gameStarted{}, nextHeartbeat{}, lastPing{}, nextMovement{};
    std::optional<uint32_t> initializedNpc;
    Clock::time_point npcDeadline{}, waypointDeadline{}, playerTradeDeadline{};
    struct PortalRequest {
        std::optional<OnlineSkillSelection> previous;
        std::set<uint32_t> existing;
        Clock::time_point deadline;
    };
    std::optional<PortalRequest> portalRequest;
    Clock::time_point itemDeadline{}, nextItem{}, storageDeadline{};
    uint64_t itemSequence{}, errorSequence{};
    Clock::time_point combatDeadline{}, nextCombat{};
    Clock::time_point respawnDeadline{};
    uint64_t combatSequence{};
    bool waitingChat{}, environmentSent{}, awaitingPong{}, registering{};
    std::string listFilter;
    explicit Impl(bool retain) : retainGamePackets(retain) {}
    ~Impl() {
        worker.request_stop();
        wake.notify_all();
        if (worker.joinable()) worker.join();
        clear_credentials();
    }
    void service() {
        try { tick(); }
        catch (const ProtocolError &e) { fail(OnlineErrorKind::Protocol, e.what()); }
        catch (const std::exception &) { fail(OnlineErrorKind::Protocol, "Network processing failed"); }
    }
    void start() {
        worker = std::jthread([this](std::stop_token stop) {
            std::unique_lock lock(mutex);
            while (!stop.stop_requested()) {
                service();
                // Timed waits release the lock; UI work never owns this worker.
                wake.wait_for(lock, std::chrono::milliseconds(10), [&] { return stop.stop_requested(); });
            }
        });
    }

    void changed() { ++view.revision; }
    void stage(OnlineStage value) {
        view.stage = value;
        if (value != OnlineStage::Lobby && value != OnlineStage::ListingGames) {
            gameInfoRequest = 0;
            view.gameInfo.reset();
        }
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
    void clear_game() {
        gs.close();
        gamePackets.reset();
        worldPackets.clear();
        worldBytes = 0;
        ++view.gameGeneration;
        view.load = {};
        view.world.clear();
        view.latencyMilliseconds.reset();
        nextMovement = {};
        nextItem = {}; itemDeadline = {};
        nextCombat = {}; combatDeadline = {};
        storageDeadline = {}; npcDeadline = {}; respawnDeadline = {}; waypointDeadline = {};
        playerTradeDeadline = {};
        gameStarted = {}; nextHeartbeat = {}; lastPing = {};
        initializedNpc.reset(); portalRequest.reset();
        environmentSent = false;
        awaitingPong = false;
        ticketHash = 0;
        ticketToken = 0;
    }
    void shutdown() {
        sid.close();
        mcp.close();
        sidPackets.reset();
        mcpPackets.reset();
        clear_game();
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
        view.error = OnlineError{kind, id, code, std::move(message), ++errorSequence};
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
        if (view.stage == OnlineStage::Failed && view.error) return false;
        error(OnlineErrorKind::Input, "Operation is unavailable in the current connection stage");
        return false;
    }
    bool current_game(const OnlineView &published) const {
        return published.connectionGeneration == view.connectionGeneration &&
            published.gameGeneration == view.gameGeneration &&
            published.world.areaGeneration == view.world.areaGeneration &&
            published.load.playerUnitId == view.load.playerUnitId;
    }
    bool require_game(const OnlineView &published) {
        if (!require(OnlineStage::ProtocolReady)) return false;
        // A background receive can change areas while the client loads resources
        // or handles an input frame. Never send that frame to a new world/GUID.
        if (!current_game(published)) {
            error(OnlineErrorKind::Input, "The active game or area changed; refresh before submitting input");
            return false;
        }
        return true;
    }
    bool require_intent(const OnlineIntentContext &context, bool interaction) {
        if (!require(OnlineStage::ProtocolReady)) return false;
        if (!(interaction ? onlineInteractionMatches(context, view) : onlineWorldMatches(context, view))) {
            error(OnlineErrorKind::Input, "Queued intent belongs to a previous game, area or interaction");
            return false;
        }
        return true;
    }
    bool require_navigation(const OnlineIntentContext &context) {
        if (!require_intent(context, true)) return false;
        if (view.world.playerTrade.active()) {
            error(OnlineErrorKind::Input, "Close the server player trade before moving or changing interaction");
            return false;
        }
        if (portalRequest) {
            error(OnlineErrorKind::Input, "Wait for the pending town portal before moving or changing interaction");
            return false;
        }
        if (view.world.itemRequest && view.world.itemRequest->state == OnlineItemRequest::State::Pending &&
            view.world.itemRequest->command.action != OnlineItemAction::Pickup) {
            error(OnlineErrorKind::Input, "Wait for the pending item response before moving or changing interaction");
            return false;
        }
        return true;
    }
    void finish_pickup(OnlineItemRequest::State state) {
        auto &world = view.world;
        auto &request = world.itemRequest;
        if (!request || request->state != OnlineItemRequest::State::Pending ||
            request->command.action != OnlineItemAction::Pickup) return;
        request->state = state;
        if (world.movementRequest && world.movementRequest->unit == OnlineUnitKey{4, request->command.item})
            world.movementRequest.reset();
        ++world.revision;
    }
    void observed(OnlineProtocolCounters &counters, const Packet &packet, size_t header) {
        ++counters.received[packet.id];
        counters.receivedBytes += packet.body.size() + header;
        counters.lastReceived = packet.id;
        changed();
    }
    void sent(ByteStream &stream, Bytes bytes) {
        const auto byteCount = bytes.size();
        const size_t idOffset = &stream == &gs ? 0 : &stream == &sid ? 1 : 2;
        const auto id = bytes.size() > idOffset ? std::optional<uint8_t>{bytes[idOffset]} : std::nullopt;
        if (!stream.send(std::move(bytes)))
            throw ProtocolError("Protocol send queue unavailable");
        auto &counts = &stream == &sid ? view.sidProtocol : &stream == &mcp ? view.mcpProtocol : view.gameProtocol;
        if (id) ++counts.sent[*id];
        counts.sentBytes += byteCount;
        changed();
    }
    void send_sid(uint8_t id, Writer out = {}) { sent(sid, frame(Framing::Sid, id, out.release())); }
    void send_mcp(uint8_t id, Writer out = {}) { sent(mcp, frame(Framing::Mcp, id, out.release())); }
    uint16_t next_request_id() {
        // IDs are real MCP wire IDs; no automatic wrap/reuse within one login.
        if (requestCounter == std::numeric_limits<uint16_t>::max())
            throw ProtocolError("MCP request IDs exhausted; reconnect required");
        return ++requestCounter;
    }
    uint16_t request_id() {
        pendingRequest = next_request_id();
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
        out.u32(1024);
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
        observed(view.sidProtocol, packet, 4);
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
            // PvPGN delivers its welcome/MOTD when entering the initial channel.
            Writer channel;
            channel.u32(1); // CLIENT_JOINCHANNEL_GENERIC
            channel.string("", 32);
            send_sid(0x0C, std::move(channel));
            Writer out;
            out.string(pendingCharacter, 15);
            send_mcp(0x07, std::move(out));
            deadline = Clock::now() + options.timeout;
            break;
        }
        case 0x0F: {
            const auto type = in.u32();
            if (type != 0x12 && type != 0x13) {
                ++view.sidProtocol.unconsumed[packet.id];
                break;
            }
            in.take(20); // Flags, latency, IP, account and authority fields.
            in.string(64);
            auto message = in.string(512);
            in.finish();
            if (!message.empty()) {
                if (view.lobbyNotices.size() == 32)
                    view.lobbyNotices.erase(view.lobbyNotices.begin());
                view.lobbyNotices.push_back({std::move(message), type == 0x13});
                changed();
            }
            break;
        }
        default:
            ++view.sidProtocol.unconsumed[packet.id];
            break; // Framed but not semantically supported; expose the ID without payload.
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
        observed(view.mcpProtocol, packet, 3);
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
            if (count > requested)
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
            clear_game();
            ticketToken = token;
            ticketHash = hash;
            erase_secret(gamePassword);
            gameName.clear();
            // Original Realm clients terminate MCP before opening the game connection.
            mcp.close();
            mcpPackets.reset();
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
        case 0x06: {
            const auto request = in.u16();
            if (!gameInfoRequest || request != gameInfoRequest || !view.gameInfo ||
                (view.stage != OnlineStage::Lobby && view.stage != OnlineStage::ListingGames)) return;
            auto info = *view.gameInfo;
            info.flags = in.u32(); info.uptimeSeconds = in.u32();
            info.creatorLevel = in.u8(); info.levelDifference = in.u8();
            info.maximumPlayers = in.u8();
            const auto count = in.u8();
            // PvPGN d2cs_protocol.h reserves 16 class and 16 level bytes,
            // regardless of current occupancy; the game still allows 1..8.
            const auto classes = in.take(16), levels = in.take(16);
            if (!info.maximumPlayers || info.maximumPlayers > 8 || count > 8 ||
                count > info.maximumPlayers || ((info.flags >> 12) & 7) > 2)
                throw ProtocolError("Invalid MCP game information");
            info.description = in.string(256);
            info.players.clear();
            for (size_t i = 0; i < count; ++i) {
                if (classes[i] >= 7) throw ProtocolError("Invalid MCP game player class");
                info.players.push_back({in.string(15), classes[i], levels[i]});
            }
            finish_zero_padding(in);
            info.state = OnlineGameInfo::State::Ready;
            view.gameInfo = std::move(info);
            gameInfoRequest = 0;
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
            ++view.mcpProtocol.unconsumed[packet.id];
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
        mcp.close();
        mcpPackets.reset();
        erase_secret(gamePassword);
        gameName.clear();
        clear_game();
        view.games.clear();
        view.gameListComplete = false;
        view.gameQueuePosition.reset();
        view.selectedCharacter.clear();
        selected.reset();
        if (directRealm) {
            realmStartup = preauthenticatedRealmStartup();
            mcp.connect(*directRealm, options.timeout);
            stage(OnlineStage::ConnectingRealm);
        } else {
            auto realm = view.selectedRealm;
            stage(OnlineStage::RealmSelection);
            choose_realm(std::move(realm));
        }
    }
    void reset_area(bool preserveInitialPosition) {
        auto &world = view.world;
        ++world.interactionGeneration;
        world.playerTrade = {}; playerTradeDeadline = {};
        const auto player = view.load.playerUnitId;
        std::erase_if(world.units, [&](const auto &entry) {
            return entry.first.type != 0 || !player || entry.first.id != *player;
        });
        std::erase_if(world.equipment,
                      [&](const auto &entry) { return !player || entry.second.owner != *player; });
        world.rooms.clear();
        world.roomAssignmentRevisions.clear();
        world.mapEvents.clear();
        world.mapEventSequence = 0;
        world.waypointSource.reset();
        world.waypointRequested.reset(); waypointDeadline = {};
        world.storage = {}; world.shopRequested.reset(); world.shopSource.reset(); world.shopGamble = false; world.tradeResult.reset();
        // Keep this connection's inventory and the socket children of retained hosts.
        std::set<uint32_t> retained;
        for (const auto &[id, item] : world.items)
            if (item.ownerType == 0 && (!player || !item.owner || item.owner == player)) retained.insert(id);
        for (int depth = 0; depth < 7; ++depth)
            for (const auto &[id, item] : world.items)
                if (item.ownerType == 4 && item.owner && retained.contains(*item.owner)) retained.insert(id);
        std::erase_if(world.items, [&](const auto &entry) { return !retained.contains(entry.first); });
        ++world.itemRevision;
        if (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending)
            world.itemRequest->state = OnlineItemRequest::State::Interrupted;
        if (world.combatRequest && world.combatRequest->state == OnlineCombatRequest::State::Pending)
            world.combatRequest->state = OnlineCombatRequest::State::Interrupted;
        world.combatEvents.clear(); world.combatSequence = 0;
        world.questAlerts.clear();
        world.npcRequested.reset(); world.npcConversation.reset(); world.movementRequest.reset();
        world.townPortalPending = false;
        initializedNpc.reset(); portalRequest.reset();
        if (!preserveInitialPosition) {
            world.playerPosition.reset();
            for (auto &[key, unit] : world.units) {
                (void)key;
                unit.position.reset();
                unit.destination.reset();
                unit.destinationUnit.reset();
            }
        }
        world.mapInitialPlayerPosition = world.playerPosition;
        ++world.areaGeneration;
        ++world.revision;
    }
    void handle_game(Packet packet) {
        observed(view.gameProtocol, packet, 1);
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
            if (*view.load.difficulty > 2 || expansion > 1 || ladder > 1)
                throw ProtocolError("Invalid game initialization flags or difficulty");
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
            // 1.13c sends the saved player, stats and equipped items before LOADACT.
            // Area changes discard spatial entities, not the loaded character.
            reset_area(view.world.areaGeneration == 0);
            view.load.serverLoadComplete = false;
            view.load.act = in.u8();
            if (*view.load.act > 4) throw ProtocolError("Invalid LoD act index");
            view.load.mapSeed = in.u32();
            view.load.townArea = in.u16();
            // Preserve the secondary value without claiming cross-version DRLG semantics.
            view.load.secondarySeed = in.u32();
            in.finish();
            changed();
        } else if (packet.id == 0x04) {
            view.load.serverLoadComplete = true;
            changed();
        } else if (packet.id == 0x05) {
            reset_area(false);
            view.load.serverLoadComplete = false;
            view.load.act.reset();
            view.load.mapSeed.reset();
            view.load.townArea.reset();
            view.load.secondarySeed.reset();
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
        try {
            if (packet.id != 0x01 && packet.id != 0x02 && packet.id != 0x03 &&
                packet.id != 0x04 && packet.id != 0x05 && packet.id != 0xAF)
                apply_world_packet(view, packet);
        } catch (const ProtocolError &error) {
            fail(OnlineErrorKind::Protocol, error.what(), packet.id);
            return;
        }
        if (auto &request = view.world.itemRequest; request && request->state == OnlineItemRequest::State::Pending) {
            const auto action = request->command.action;
            const bool merchant = action == OnlineItemAction::Buy || action == OnlineItemAction::Sell ||
                action == OnlineItemAction::Repair || action == OnlineItemAction::RepairAll || action == OnlineItemAction::IdentifyAll;
            bool related = packet.id == 0x97 && action == OnlineItemAction::SwitchWeapons;
            related |= packet.id == 0x77 && action == OnlineItemAction::CubeOpen && view.world.storage.kind == OnlineStorageKind::Cube;
            related |= packet.id == 0x9C && action == OnlineItemAction::TradeOpen && view.world.shopSource == request->command.npc;
            if ((packet.id == 0x1D || packet.id == 0x1E || packet.id == 0x1F) && !packet.body.empty()) {
                const auto stat = packet.body.front();
                related |= (stat == 14 || stat == 15) && (action == OnlineItemAction::GoldDeposit ||
                    action == OnlineItemAction::GoldWithdraw || action == OnlineItemAction::GoldDrop);
            }
            if (packet.id == 0x2A && merchant && view.world.tradeResult) {
                request->state = view.world.tradeResult->result >= 7 ? OnlineItemRequest::State::Rejected : OnlineItemRequest::State::Updated;
                if (request->state == OnlineItemRequest::State::Rejected)
                    error(OnlineErrorKind::Server, "Original NPC transaction was rejected", 0x2A, view.world.tradeResult->result);
            }
            Reader itemPacket(packet.body);
            std::optional<uint32_t> id;
            if (packet.id == 0x9C || packet.id == 0x9D) {
                itemPacket.take(3); id = itemPacket.u32();
            } else if (packet.id == 0x0A || packet.id == 0x42) {
                const auto ownerType = itemPacket.u8();
                const auto owner = itemPacket.u32();
                if (packet.id == 0x0A && ownerType == 4) id = owner;
                if (packet.id == 0x42 && ((ownerType == 0 && owner == view.load.playerUnitId) || ownerType == 6) &&
                    !view.world.items.contains(request->command.item)) id = request->command.item;
            } else if (packet.id == 0x3E) {
                itemPacket.u8(); BitReader bits(itemPacket.take(itemPacket.remaining()));
                id = bits.read(bits.read(1) ? (bits.read(1) ? 32 : 16) : 8);
            }
            related |= id && ((request->command.itemRevision && *id == request->command.item) ||
                (request->command.targetRevision && *id == request->command.target));
            if ((packet.id == 0x9C || packet.id == 0x9D || packet.id == 0x0A || packet.id == 0x42 || packet.id == 0x3E) &&
                action == OnlineItemAction::Transmute) related = true;
            // Merchant 0x2A is the explicit result; preliminary item packets are not its ACK.
            if (merchant && packet.id != 0x2A) related = false;
            if (related && request->state == OnlineItemRequest::State::Pending) {
                if (action == OnlineItemAction::Pickup) finish_pickup(OnlineItemRequest::State::Updated);
                else request->state = OnlineItemRequest::State::Updated;
            }
            else if (!view.world.playerPosition || onlinePlayerDead(view.world))
                request->state = OnlineItemRequest::State::Interrupted;
        }
        if (auto &request = view.world.combatRequest; request && request->state == OnlineCombatRequest::State::Pending) {
            const auto &command = request->command;
            bool confirmed = false;
            if (command.action == OnlineCombatCommand::Action::SelectSkill && packet.id == 0x23) {
                const auto selectedSkill = command.hand == OnlineSkillHand::Left ? view.world.leftSkill : view.world.rightSkill;
                Reader ack(packet.body);
                const auto type = ack.u8(); const auto owner = ack.u32(); const auto left = ack.u8();
                confirmed = type == 0 && owner == view.load.playerUnitId &&
                    bool(left) == (command.hand == OnlineSkillHand::Left) &&
                    selectedSkill && *selectedSkill == OnlineSkillSelection{command.skill, UINT32_MAX};
            } else if (command.action == OnlineCombatCommand::Action::LearnSkill && packet.id == 0x21) {
                const auto skill = view.world.playerBaseSkills.find(command.skill);
                confirmed = skill != view.world.playerBaseSkills.end() && skill->second > request->before;
            } else if (command.action == OnlineCombatCommand::Action::SpendAttribute &&
                       (packet.id == 0x1D || packet.id == 0x1E || packet.id == 0x1F)) {
                const auto stat = view.world.playerAttributes.find(command.attribute);
                confirmed = stat != view.world.playerAttributes.end() && stat->second >= request->before + command.count;
            }
            if (confirmed) request->state = OnlineCombatRequest::State::Confirmed;
            else if (!view.world.playerPosition || onlinePlayerDead(view.world))
                request->state = OnlineCombatRequest::State::Interrupted;
        }
        if (!view.world.playerPosition || onlinePlayerDead(view.world)) {
            if (view.world.storage.kind != OnlineStorageKind::None || view.world.storage.requested != OnlineStorageKind::None ||
                view.world.shopRequested || view.world.shopSource || view.world.npcRequested ||
                view.world.npcConversation || view.world.waypointSource || view.world.waypointRequested)
                ++view.world.interactionGeneration;
            portalRequest.reset(); view.world.townPortalPending = false;
            view.world.storage = {}; view.world.shopRequested.reset(); view.world.shopSource.reset(); view.world.shopGamble = false;
            view.world.movementRequest.reset(); view.world.npcRequested.reset(); view.world.npcConversation.reset();
            view.world.waypointSource.reset(); view.world.waypointRequested.reset(); waypointDeadline = {}; initializedNpc.reset();
        }
        if (packet.id == 0x63) {
            Reader menu(packet.body);
            const auto source = menu.u32();
            if (view.world.waypointSource == source) waypointDeadline = {};
            else if (!view.world.waypointSource && !view.world.waypointRequested && !view.world.npcRequested &&
                     view.world.storage.kind == OnlineStorageKind::None &&
                     view.world.storage.requested == OnlineStorageKind::None && view.world.playerPosition &&
                     !onlinePlayerDead(view.world)) {
                // A delayed native operate can open after an earlier close. Retire
                // it only while no newer interaction owns the player's lock.
                Writer close; close.u8(0x49); close.u32(source); close.u32(0);
                sent(gs, close.release());
            }
        }
        if (packet.id == 0x27 && view.world.npcConversation &&
            initializedNpc != view.world.npcConversation->source) {
            Writer init;
            init.u8(0x2F); init.u32(1); init.u32(view.world.npcConversation->source);
            sent(gs, init.release());
            initializedNpc = view.world.npcConversation->source;
        }
        if (packet.id == 0x82 && portalRequest && view.load.playerUnitId) {
            for (const auto &[key, entry] : view.world.units)
                if (key.type == 2 && entry.portalOwner == *view.load.playerUnitId &&
                    !portalRequest->existing.contains(key.id)) {
                    restore_portal_skill();
                    break;
                }
        }
        // The replica already consumed the ordered packet. Optional raw consumers
        // must drain their bounded queue; the UI client uses only value snapshots.
        if (retainGamePackets) {
            if (worldPackets.size() >= 4096 || packet.body.size() + 1 > 2 * 1024 * 1024 - worldBytes)
                throw ProtocolError("World packet queue limit exceeded; drain packets regularly");
            worldBytes += packet.body.size() + 1;
            worldPackets.push_back({view.gameGeneration, std::move(packet)});
        }
        if (view.stage == OnlineStage::LoadingGame && environmentSent && view.load.serverLoadComplete &&
            view.load.act && view.load.difficulty && view.load.playerUnitId && view.load.mapSeed &&
            view.world.playerPosition) {
            const auto player = view.world.units.find({0, *view.load.playerUnitId});
            if (player != view.world.units.end() && player->second.position && player->second.classId &&
                !player->second.name.empty()) {
                if (!selected || player->second.name != selected->name || player->second.classId != selected->characterClass)
                    throw ProtocolError("Assigned game player differs from the selected Realm character");
                stage(OnlineStage::ProtocolReady);
                // Rcv0x40 requests the player-private quest log/status snapshot after assignment.
                sent(gs, {0x40});
            }
        }
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
                    const auto id = packet.id;
                    try { handle_sid(std::move(packet)); }
                    catch (const ProtocolError &e) {
                        fail(OnlineErrorKind::Protocol, e.what(), id);
                        return;
                    }
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
                    const auto id = packet.id;
                    try { handle_mcp(std::move(packet)); }
                    catch (const ProtocolError &e) {
                        fail(OnlineErrorKind::Protocol, e.what(), id);
                        return;
                    }
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
                    const auto id = packet.id;
                    try { handle_game(std::move(packet)); }
                    catch (const ProtocolError &e) {
                        fail(OnlineErrorKind::Protocol, e.what(), id);
                        return;
                    }
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
    void close_interaction() {
        // A new navigation/interaction intent replaces the native pickup approach.
        // Retire only its wait/presentation target; late inventory packets still apply.
        finish_pickup(OnlineItemRequest::State::Interrupted);
        auto &world = view.world;
        if (world.npcRequested || world.npcConversation || world.waypointSource || world.waypointRequested || world.shopRequested ||
            world.shopSource || world.storage.kind != OnlineStorageKind::None || world.storage.requested != OnlineStorageKind::None)
            ++world.interactionGeneration;
        const auto storage = world.storage.kind != OnlineStorageKind::None ? world.storage.kind : world.storage.requested;
        if (storage != OnlineStorageKind::None) {
            Writer close;
            close.u8(0x4F); close.u16(storage == OnlineStorageKind::Stash ? 18 : 23); close.u16(0); close.u16(0);
            sent(gs, close.release());
        }
        world.storage = {}; storageDeadline = {};
        world.shopRequested.reset(); world.shopSource.reset(); world.shopGamble = false;
        std::erase_if(world.items, [](const auto &entry) { return entry.second.action == 11; });
        ++world.itemRevision;
        if (world.npcRequested) {
            Writer close;
            close.u8(0x30); close.u32(1); close.u32(*world.npcRequested);
            sent(gs, close.release());
        }
        const auto waypoint = world.waypointSource ? world.waypointSource : world.waypointRequested;
        if (waypoint) {
            Writer close;
            close.u8(0x49); close.u32(*waypoint); close.u32(0);
            sent(gs, close.release());
        }
        world.npcRequested.reset(); world.npcConversation.reset(); world.waypointSource.reset();
        world.waypointRequested.reset(); waypointDeadline = {};
        initializedNpc.reset();
        npcDeadline = {};
        ++world.revision;
    }
    void restore_portal_skill() {
        const auto previous = portalRequest->previous;
        portalRequest.reset();
        view.world.townPortalPending = false;
        if (previous) {
            Writer select;
            select.u8(0x3C); select.u32(previous->skill); select.u32(previous->owner);
            sent(gs, select.release());
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
        if (gameInfoRequest && now >= gameInfoDeadline) {
            gameInfoRequest = 0;
            if (view.gameInfo) view.gameInfo->state = OnlineGameInfo::State::TimedOut;
            changed(); // Missing/deleted games send no reply; joining by name remains available.
        }
        if (auto &request = view.world.respawnRequest; request &&
            (request->state == OnlineRespawnRequest::State::WaitingForDeath || request->state == OnlineRespawnRequest::State::Sent)) {
            if (now >= respawnDeadline) {
                request->state = OnlineRespawnRequest::State::TimedOut;
                error(OnlineErrorKind::Timeout, "No server resurrection confirmation arrived; current death state is retained");
            } else if (request->state == OnlineRespawnRequest::State::WaitingForDeath &&
                       view.stage == OnlineStage::ProtocolReady && view.world.deathPhase == OnlineDeathPhase::Dead) {
                sent(gs, {0x41}); request->state = OnlineRespawnRequest::State::Sent; request->sent = true;
                request->revision = ++view.world.revision; changed();
            }
        }
        if (view.world.storage.requested != OnlineStorageKind::None && now >= storageDeadline) {
            close_interaction();
            error(OnlineErrorKind::Timeout, "No original stash or cube open confirmation arrived");
        }
        auto &playerTrade = view.world.playerTrade;
        if (playerTrade.active() && (playerTrade.response == OnlinePlayerTrade::Response::GoldSent ||
            playerTrade.response == OnlinePlayerTrade::Response::ResetSent ||
            playerTrade.response == OnlinePlayerTrade::Response::AcceptSent ||
            playerTrade.response == OnlinePlayerTrade::Response::CancelSent) && now >= playerTradeDeadline) {
            playerTrade.response = OnlinePlayerTrade::Response::TimedOut;
            error(OnlineErrorKind::Timeout, "No server player trade confirmation arrived; state is retained, without retrying acceptance");
        }
        if (view.world.waypointRequested && now >= waypointDeadline) {
            close_interaction();
            error(OnlineErrorKind::Timeout, "No original waypoint menu confirmation arrived");
        }
        if (view.world.itemRequest && view.world.itemRequest->state == OnlineItemRequest::State::Pending && now >= itemDeadline) {
            finish_pickup(OnlineItemRequest::State::TimedOut);
            view.world.itemRequest->state = OnlineItemRequest::State::TimedOut;
            error(OnlineErrorKind::Timeout, "No related server item update arrived; inspect current inventory before retrying");
        }
        if (view.world.combatRequest && view.world.combatRequest->state == OnlineCombatRequest::State::Pending &&
            now >= combatDeadline) {
            view.world.combatRequest->state = OnlineCombatRequest::State::TimedOut;
            error(OnlineErrorKind::Timeout, "No matching server skill or attribute confirmation arrived; inspect current state");
        }
        if (view.world.npcRequested && !view.world.npcConversation && now >= npcDeadline) {
            close_interaction();
            error(OnlineErrorKind::Timeout, "NPC did not return a conversation before timeout");
        }
        if (portalRequest && now >= portalRequest->deadline) {
            restore_portal_skill();
            error(OnlineErrorKind::Timeout, "No new server-owned town portal was assigned");
        }
        if (pending_stage(view.stage) && now >= deadline) {
            if (view.stage == OnlineStage::ListingGames) {
                // PvPGN can send no terminator for an empty list. Keep partial rows;
                // absence of the terminator never establishes a complete empty list.
                error(OnlineErrorKind::Timeout, "Game list did not finish before timeout", 0x05);
                stage(OnlineStage::Lobby);
            } else if (view.stage == OnlineStage::CreatingGame || view.stage == OnlineStage::JoiningGame ||
                       view.stage == OnlineStage::ConnectingGame || view.stage == OnlineStage::GameHandshake ||
                       view.stage == OnlineStage::LeavingGame) {
                const bool unconfirmedLeave = view.stage == OnlineStage::LeavingGame;
                return_to_realm();
                error(OnlineErrorKind::Timeout, unconfirmedLeave
                    ? "Server leave was not confirmed; save result is unknown; reopening the character list"
                    : "Game request timed out; reopening the server character list");
            } else if (view.stage == OnlineStage::LoadingGame) {
                close_interaction();
                sent(gs, {0x69});
                stage(OnlineStage::LeavingGame);
                error(OnlineErrorKind::Timeout, "Game loading timed out; waiting for server save and leave");
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
                sent(gs, game_ping(uint32_t(elapsed), view.latencyMilliseconds.value_or(0)));
                lastPing = now;
                awaitingPong = true;
            }
            nextHeartbeat = now + options.heartbeat;
        }
    }
};
RealmSession::RealmSession(bool retainGamePackets) : impl_(std::make_unique<Impl>(retainGamePackets)) {
    impl_->start();
}
void RealmSession::connect_realm(std::unique_ptr<IByteTransport> realmTransport,
    std::unique_ptr<IByteTransport> gameTransport, Endpoint endpoint, std::string name, uint16_t gamePort) {
    std::lock_guard lock(impl_->mutex);
    auto &p = *impl_;
    const auto connection = p.view.connectionGeneration + 1, game = p.view.gameGeneration + 1, revision = p.view.revision;
    p.shutdown(); p.view.clear(); p.view.connectionGeneration = connection; p.view.gameGeneration = game;
    p.view.revision = revision; p.requestCounter = 0; p.lastListRequest = 0;
    p.options = LoginOptions{};
    if (!gamePort) throw ProtocolError("Zero native game port");
    p.options.gamePort = gamePort;
    p.directRealm = endpoint;
    p.mcp.use(std::move(realmTransport)); p.gs.use(std::move(gameTransport));
    p.view.selectedRealm = std::move(name);
    p.realmStartup = preauthenticatedRealmStartup();
    p.mcp.connect(std::move(endpoint), p.options.timeout);
    p.stage(OnlineStage::ConnectingRealm);
    p.snapshotDirty = true;
}
RealmSession::~RealmSession() = default;
void RealmSession::login(LoginOptions options) {
    authenticate(std::move(options), false);
}
void RealmSession::register_account(LoginOptions options) {
    authenticate(std::move(options), true);
}
void RealmSession::authenticate(LoginOptions options, bool createAccount) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    p.shutdown();
    p.directRealm.reset();
    p.mcp.use(std::make_unique<TcpStream>()); p.gs.use(std::make_unique<TcpStream>());
    const auto revision = p.view.revision, generation = p.view.connectionGeneration,
               gameGeneration = p.view.gameGeneration;
    p.view.clear();
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
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    try {
        return impl_->choose_realm(std::move(name));
    } catch (const std::exception &) {
        impl_->fail(OnlineErrorKind::Protocol, "Realm request could not be encoded");
        return false;
    }
}
bool RealmSession::return_to_realms() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
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
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require(OnlineStage::CharacterSelection))
        return false;
    if (!character_name_valid(options.name) || options.characterClass > 6) {
        p.error(OnlineErrorKind::Input,
                "Use 1-15 ASCII letters, digits, hyphen or underscore; the server validates its naming rules");
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
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
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
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
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
        if (p.directRealm) {
            Writer selected;
            selected.string(p.pendingCharacter, 15);
            p.send_mcp(0x07, std::move(selected));
            p.waitingChat = false;
        } else {
            p.send_sid(0x0A, std::move(out));
            p.waitingChat = true;
        }
        p.stage(OnlineStage::SelectingCharacter);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Protocol, "Character request could not be encoded");
        return false;
    }
}
bool RealmSession::list_games(std::string filter) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (p.view.stage != OnlineStage::Lobby && p.view.stage != OnlineStage::ListingGames)
        return p.require(OnlineStage::Lobby);
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
        p.view.gameInfo.reset(); p.gameInfoRequest = 0;
        p.view.gameListComplete = false;
        p.view.error.reset();
        p.stage(OnlineStage::ListingGames);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Protocol, "Game list request could not be encoded");
        return false;
    }
}
bool RealmSession::query_game(std::string name) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (p.view.stage != OnlineStage::Lobby && p.view.stage != OnlineStage::ListingGames)
        return p.require(OnlineStage::Lobby);
    if (!text_valid(name, 15)) {
        p.error(OnlineErrorKind::Input, "Invalid game information name"); return false;
    }
    try {
        Writer out;
        p.gameInfoRequest = p.next_request_id();
        out.u16(p.gameInfoRequest); out.string(name, 15);
        p.send_mcp(0x06, std::move(out));
        p.view.gameInfo.emplace(); p.view.gameInfo->name = std::move(name);
        p.gameInfoDeadline = Clock::now() + p.options.timeout;
        p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Protocol, "Game information request could not be encoded"); return false;
    }
}
bool RealmSession::cancel_game_list() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
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
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
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
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (p.view.stage != OnlineStage::Lobby && p.view.stage != OnlineStage::ListingGames) {
        p.require(OnlineStage::Lobby);
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
bool RealmSession::send_chat(std::string text) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require(OnlineStage::ProtocolReady)) return false;
    // A queued UI command must not cross login/game replacement. Area changes
    // and death do not change the destination room or authorize local gameplay.
    if (snapshot_.connectionGeneration != p.view.connectionGeneration ||
        snapshot_.gameGeneration != p.view.gameGeneration ||
        snapshot_.load.playerUnitId != p.view.load.playerUnitId) {
        p.error(OnlineErrorKind::Input, "The active game changed; refresh before sending chat");
        return false;
    }
    if (!text_valid(text, 255) ||
        std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == ' '; })) {
        p.error(OnlineErrorKind::Input, "Chat requires 1-255 printable ASCII bytes and a non-space character");
        return false;
    }
    try {
        p.sent(p.gs, game_chat(text));
        // No generic native ACK and no client echo; the original 0x26 is the
        // only source of messages, including the sender's own room broadcast.
        p.view.error.reset();
        p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Chat message could not be queued");
        return false;
    }
}
bool RealmSession::respond_player_trade(bool accept, uint64_t revision, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    auto &trade = p.view.world.playerTrade;
    using Phase = OnlinePlayerTrade::Phase;
    using Response = OnlinePlayerTrade::Response;
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !trade.active() || trade.revision != revision ||
        (accept && (trade.phase != Phase::Incoming || trade.response != Response::None || onlinePlayerDead(p.view.world))) ||
        (!accept && trade.response == Response::CancelSent)) {
        p.error(OnlineErrorKind::Input, "The server trade invitation changed or already has a pending response"); return false;
    }
    try {
        // PlrMsg Rcv0x4F / PlrTrade sub_6FC91250: button 3 begins the trade;
        // button 2 cancels. Button 4 would accept offered items and is not sent.
        Writer out; out.u8(0x4F); out.u16(accept ? 3 : 2); out.u16(0); out.u16(0);
        p.sent(p.gs, out.release());
        trade.response = accept ? Response::AcceptSent : Response::CancelSent;
        p.playerTradeDeadline = Clock::now() + p.options.timeout;
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Player trade response could not be queued"); return false;
    }
}
bool RealmSession::update_player_trade(OnlinePlayerTradeAction action, uint64_t revision, uint32_t amount,
                                      std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    auto &world = p.view.world; auto &trade = world.playerTrade;
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || onlinePlayerDead(world) ||
        trade.phase != OnlinePlayerTrade::Phase::Open || !trade.peer || trade.revision != revision ||
        trade.response != OnlinePlayerTrade::Response::None ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending) ||
        std::any_of(world.items.begin(), world.items.end(), [&](const auto &entry) {
            return entry.second.ownerType == 0 && entry.second.owner == p.view.load.playerUnitId && entry.second.mode == 4;
        })) {
        p.error(OnlineErrorKind::Input, "Trade offer changed, has a cursor item, or is waiting for the server"); return false;
    }
    if ((action == OnlinePlayerTradeAction::Agree && (trade.ownAgreed || trade.agreementLocked)) ||
        (action == OnlinePlayerTradeAction::Gold && trade.ownAgreed) ||
        (action == OnlinePlayerTradeAction::Revoke && !trade.ownAgreed && !trade.peerAgreed)) {
        p.error(OnlineErrorKind::Input, "Reset the current agreement before changing this trade offer"); return false;
    }
    if (action == OnlinePlayerTradeAction::Gold) {
        const auto gold = world.playerAttributes.find(14);
        if (gold == world.playerAttributes.end() || amount > gold->second || amount > INT32_MAX) {
            p.error(OnlineErrorKind::Input, "Trade gold exceeds the known server wallet"); return false;
        }
    }
    try {
        // D2MOO PlrTrade: 4 accepts offers, 7 resets both checks, 8 quotes gold.
        if (action == OnlinePlayerTradeAction::Gold && trade.peerAgreed) {
            Writer reset; reset.u8(0x4F); reset.u16(7); reset.u16(0); reset.u16(0);
            p.sent(p.gs,reset.release());
        }
        Writer out; out.u8(0x4F);
        out.u16(action == OnlinePlayerTradeAction::Agree ? 4 : action == OnlinePlayerTradeAction::Revoke ? 7 : 8);
        out.u16(uint16_t(amount >> 16)); out.u16(uint16_t(amount));
        p.sent(p.gs, out.release());
        if (action == OnlinePlayerTradeAction::Agree) trade.ownAgreed = true; // Sent; only 0x77/13 confirms completion.
        else {
            trade.response = action == OnlinePlayerTradeAction::Gold ? OnlinePlayerTrade::Response::GoldSent : OnlinePlayerTrade::Response::ResetSent;
            p.playerTradeDeadline = Clock::now() + p.options.timeout;
        }
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Player trade offer command could not be queued"); return false;
    }
}
bool RealmSession::resurrect(std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), false)) return false;
    auto &world = p.view.world;
    if (!onlinePlayerDead(world)) {
        p.error(OnlineErrorKind::Input, "The server has not reported player death"); return false;
    }
    if (p.selected && p.selected->hardcore.value_or(false)) return leave_game();
    if (world.respawnRequest && (world.respawnRequest->state == OnlineRespawnRequest::State::WaitingForDeath ||
                                world.respawnRequest->state == OnlineRespawnRequest::State::Sent)) return true;
    p.view.error.reset();
    world.movementRequest.reset(); world.npcRequested.reset(); world.npcConversation.reset();
    world.waypointSource.reset(); world.storage = {}; world.shopRequested.reset(); world.shopSource.reset(); world.shopGamble = false;
    world.waypointRequested.reset(); p.waypointDeadline = {};
    auto request = world.respawnRequest.value_or(OnlineRespawnRequest{});
    request.state = OnlineRespawnRequest::State::WaitingForDeath; request.revision = ++world.revision;
    world.respawnRequest = request;
    p.respawnDeadline = Clock::now() + p.options.timeout;
    p.changed(); return true;
}
bool RealmSession::leave_game() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (p.view.stage == OnlineStage::LeavingGame) return true;
    if (p.view.stage == OnlineStage::CreatingGame || p.view.stage == OnlineStage::JoiningGame ||
        p.view.stage == OnlineStage::ConnectingGame || p.view.stage == OnlineStage::GameHandshake) {
        try {
            // Retire MCP as well: a late create/join reply must never supply a ticket.
            p.return_to_realm();
            p.view.error.reset();
            return true;
        } catch (const std::exception &) {
            p.fail(OnlineErrorKind::Transport, "Game cancellation could not reopen the Realm");
            return false;
        }
    }
    if (p.view.stage != OnlineStage::ProtocolReady && p.view.stage != OnlineStage::LoadingGame) {
        p.error(OnlineErrorKind::Input, "No game is available to leave");
        return false;
    }
    try {
        p.close_interaction();
        p.sent(p.gs, {0x69});
        if (p.view.world.itemRequest && p.view.world.itemRequest->state == OnlineItemRequest::State::Pending)
            p.view.world.itemRequest->state = OnlineItemRequest::State::Interrupted;
        p.portalRequest.reset();
        p.view.world.townPortalPending = false;
        p.view.error.reset();
        p.stage(OnlineStage::LeavingGame);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Game leave request could not be queued");
        return false;
    }
}
bool RealmSession::move_to(OnlinePoint target, bool run, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_))))
        return false;
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world)) {
        p.error(OnlineErrorKind::Input, "The server player is not available to move");
        return false;
    }
    if (Clock::now() < p.nextMovement)
        return false;
    try {
        Writer out;
        out.u8(run ? 0x03 : 0x01);
        out.u16(target.x);
        out.u16(target.y);
        p.close_interaction();
        p.sent(p.gs, out.release());
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.world.movementRequest = OnlineMovementRequest{target, {}, run, ++p.view.world.revision};
        p.view.error.reset();
        p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Movement request could not be queued");
        return false;
    }
}
bool RealmSession::use_exit(uint32_t serverUnitId) {
    return interact_map_unit({5, serverUnitId});
}
bool RealmSession::move_to_unit(OnlineUnitKey target, bool run, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto found = p.view.world.units.find(target);
    const bool corpse = target.type == 0 && p.view.world.corpseOwners.contains(target.id) &&
        p.view.world.corpseOwners.at(target.id) == p.view.load.playerUnitId;
    const bool tradePeer = target.type == 0 && target.id != p.view.load.playerUnitId &&
        !p.view.world.corpseOwners.contains(target.id) && found != p.view.world.units.end() &&
        found->second.mode != 0 && found->second.mode != 17;
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world) || found == p.view.world.units.end() ||
        !found->second.position || !found->second.classId ||
        (target.type != 1 && target.type != 2 && target.type != 5 && !corpse && !tradePeer)) {
        p.error(OnlineErrorKind::Input, "The server movement target is unavailable"); return false;
    }
    if (Clock::now() < p.nextMovement) return false;
    try {
        p.close_interaction();
        Writer out;
        out.u8(run ? 0x04 : 0x02); out.u32(target.type); out.u32(target.id);
        p.sent(p.gs, out.release());
        p.view.world.movementRequest = OnlineMovementRequest{{}, target, run, ++p.view.world.revision};
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Unit movement request could not be queued"); return false;
    }
}
bool RealmSession::interact_map_unit(OnlineUnitKey target, OnlineObjectIntent intent, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    if (intent != OnlineObjectIntent::Operate && target.type != 2) {
        p.error(OnlineErrorKind::Input, "Stash and waypoint intents require an assigned object"); return false;
    }
    const auto found = p.view.world.units.find(target);
    const bool corpse = target.type == 0 && p.view.world.corpseOwners.contains(target.id) &&
        p.view.world.corpseOwners.at(target.id) == p.view.load.playerUnitId;
    const bool tradePeer = target.type == 0 && target.id != p.view.load.playerUnitId &&
        !p.view.world.corpseOwners.contains(target.id) && found != p.view.world.units.end() &&
        found->second.mode != 0 && found->second.mode != 17;
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world) || found == p.view.world.units.end() ||
        !found->second.position || !found->second.classId || (target.type != 2 && target.type != 5 && !corpse && !tradePeer)) {
        p.error(OnlineErrorKind::Input, "The server map unit is not available");
        return false;
    }
    if (Clock::now() < p.nextMovement) return false;
    try {
        // D2MOO PlrMsg Rcv0x13: packet byte, uint32 unit type, uint32 GUID.
        Writer out;
        out.u8(0x13); out.u32(target.type); out.u32(target.id);
        p.close_interaction();
        p.sent(p.gs, out.release());
        if (intent == OnlineObjectIntent::Stash) {
            p.view.world.storage.requested = OnlineStorageKind::Stash;
            ++p.view.world.interactionGeneration;
            p.view.world.storage.requestedSource = target.id;
            p.storageDeadline = Clock::now() + p.options.timeout;
        }
        if (intent == OnlineObjectIntent::Waypoint) {
            p.view.world.waypointRequested = target.id;
            ++p.view.world.interactionGeneration;
            p.waypointDeadline = Clock::now() + p.options.timeout;
        }
        p.view.world.movementRequest = OnlineMovementRequest{{}, target, true, ++p.view.world.revision, true};
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Map interaction could not be queued");
        return false;
    }
}
bool RealmSession::interact_npc(uint32_t serverUnitId, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto found = p.view.world.units.find({1, serverUnitId});
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world) || found == p.view.world.units.end() ||
        !found->second.position || !found->second.classId || found->second.mode == 0 || found->second.mode == 12) {
        p.error(OnlineErrorKind::Input, "The server NPC is unavailable"); return false;
    }
    if (p.view.world.npcRequested == serverUnitId) return false;
    if (Clock::now() < p.nextMovement) return false;
    try {
        p.close_interaction();
        Writer out;
        out.u8(0x13); out.u32(1); out.u32(serverUnitId);
        p.sent(p.gs, out.release());
        p.view.world.npcRequested = serverUnitId;
        ++p.view.world.interactionGeneration;
        p.npcDeadline = Clock::now() + p.options.timeout;
        p.view.world.movementRequest.reset();
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC interaction could not be queued"); return false;
    }
}
bool RealmSession::close_npc(std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    if (!p.view.world.npcRequested) {
        p.error(OnlineErrorKind::Input, "No NPC interaction is active"); return false;
    }
    try {
        p.close_interaction(); p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC close request could not be queued"); return false;
    }
}
bool RealmSession::acknowledge_npc_message(uint16_t stringId, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    auto &conversation = p.view.world.npcConversation;
    if (!conversation || !p.view.world.playerPosition || onlinePlayerDead(p.view.world) ||
        !std::any_of(conversation->messages.begin(), conversation->messages.end(),
            [&](const auto &message) { return message.stringId == stringId; }) ||
        conversation->acknowledged.contains(stringId)) {
        p.error(OnlineErrorKind::Input, "No unacknowledged server NPC message matches this ID"); return false;
    }
    try {
        Writer out;
        out.u8(0x31); out.u32(conversation->source); out.u16(stringId); out.u16(0);
        p.sent(p.gs, out.release());
        conversation->acknowledged.insert(stringId);
        ++p.view.world.revision; p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC message could not be queued"); return false;
    }
}
bool RealmSession::npc_travel(std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto &conversation = p.view.world.npcConversation;
    if (!conversation || !p.view.world.playerPosition || onlinePlayerDead(p.view.world)) {
        p.error(OnlineErrorKind::Input, "No server NPC conversation is active"); return false;
    }
    try {
        Writer out;
        out.u8(0x38); out.u32(0); out.u32(conversation->source); out.u32(0);
        p.sent(p.gs, out.release());
        p.close_interaction(); p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC travel could not be queued"); return false;
    }
}
bool RealmSession::create_town_portal(uint16_t skillId, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    const auto &world = p.view.world;
    const auto quantity = world.itemSkillQuantities.find(skillId);
    const auto skill = world.playerSkills.find(skillId);
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !world.playerPosition ||
        !world.rightSkill || onlinePlayerDead(world) ||
        p.portalRequest || (world.combatRequest && world.combatRequest->state == OnlineCombatRequest::State::Pending) ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending) ||
        (quantity != world.itemSkillQuantities.end() ? !quantity->second
            : skill == world.playerSkills.end() || !skill->second)) {
        p.error(OnlineErrorKind::Input, "A server town-portal item skill is unavailable or already pending"); return false;
    }
    if (Clock::now() < p.nextMovement) return false;
    try {
        Impl::PortalRequest request{world.rightSkill, {}, Clock::now() + p.options.timeout};
        if (request.previous && request.previous->skill == skillId) request.previous.reset();
        for (const auto &[key, entry] : world.units)
            if (key.type == 2 && entry.portalOwner == p.view.load.playerUnitId) request.existing.insert(key.id);
        const auto point = *world.playerPosition;
        p.close_interaction();
        Writer select;
        select.u8(0x3C); select.u32(skillId); select.u32(UINT32_MAX);
        p.sent(p.gs, select.release());
        Writer cast;
        cast.u8(0x0C); cast.u16(point.x); cast.u16(point.y);
        p.sent(p.gs, cast.release());
        p.portalRequest = std::move(request);
        p.view.world.townPortalPending = true;
        p.view.world.movementRequest.reset();
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Town portal could not be queued"); return false;
    }
}
bool RealmSession::submit_item(OnlineItemCommand command) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!command.context) command.context = onlineIntentContext(snapshot_);
    if (!p.require_intent(*command.context, true)) return false;
    auto &world = p.view.world;
    if (world.playerTrade.active()) {
        const auto &trade = world.playerTrade;
        const auto source = world.items.find(command.item);
        const auto target = world.items.find(command.target);
        auto accessible = [&](const OnlineItem &i) {
            return i.ownerType == 0 && i.owner == p.view.load.playerUnitId &&
                ((i.mode == 0 && (i.page == 1 || i.page == 3)) || i.mode == 4);
        };
        const bool move = command.action == OnlineItemAction::Take || command.action == OnlineItemAction::Place || command.action == OnlineItemAction::Swap;
        if (trade.phase != OnlinePlayerTrade::Phase::Open || trade.ownAgreed ||
            trade.response != OnlinePlayerTrade::Response::None || !move ||
            source == world.items.end() || !accessible(source->second) ||
            (command.action == OnlineItemAction::Place && command.page != 0 && command.page != 2) ||
            (command.action == OnlineItemAction::Swap && (target == world.items.end() || !accessible(target->second)))) {
            p.error(OnlineErrorKind::Input, "Trade permits moving only your backpack, cursor and own offer; reset your agreement first"); return false;
        }
    }
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !world.playerPosition ||
        onlinePlayerDead(world) || p.portalRequest || (world.combatRequest && world.combatRequest->state == OnlineCombatRequest::State::Pending) ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending &&
            command.action != OnlineItemAction::StorageClose)) {
        p.error(OnlineErrorKind::Input, "Server item operation is unavailable or pending"); return false;
    }
    const bool hasItem = command.action <= OnlineItemAction::Identify || command.action == OnlineItemAction::CubeOpen ||
        command.action == OnlineItemAction::Buy || command.action == OnlineItemAction::Sell || command.action == OnlineItemAction::Repair;
    const auto item = world.items.find(command.item);
    if (hasItem && (item == world.items.end() ||
        command.itemRevision != item->second.revision)) {
        p.error(OnlineErrorKind::Input, "Item identity or revision is stale"); return false;
    }
    if (command.targetRevision) {
        const auto target = world.items.find(command.target);
        if (target == world.items.end() || target->second.revision != command.targetRevision) {
            p.error(OnlineErrorKind::Input, "Target item identity or revision is stale"); return false;
        }
    }
    if (hasItem && command.action != OnlineItemAction::Pickup && command.action != OnlineItemAction::Buy &&
        (item->second.ownerType != 0 || item->second.owner != p.view.load.playerUnitId)) {
        p.error(OnlineErrorKind::Input, "Item is not owned by this server player"); return false;
    }
    if (command.action == OnlineItemAction::Buy &&
        (world.npcRequested != command.npc || world.shopSource != command.npc || world.shopGamble != command.gamble ||
         item->second.ownerType != 1 || item->second.owner != command.npc || item->second.action != 11 ||
         bool(item->second.flags & 0x2000000u) != command.gamble)) {
        p.error(OnlineErrorKind::Input, "Purchase shelf or vendor service changed before submission"); return false;
    }
    if (Clock::now() < p.nextItem) {
        p.error(OnlineErrorKind::Input, "Item request is rate limited"); return false;
    }
    try {
        Writer out;
        const auto point = *world.playerPosition;
        const auto mode = item == world.items.end() ? 0 : item->second.mode;
        auto pair = [&](uint8_t opcode) { out.u8(opcode); out.u32(command.item); out.u32(command.target); };
        switch (command.action) {
        case OnlineItemAction::Pickup:
            out.u8(0x16); out.u32(4); out.u32(command.item); out.u32(command.toCursor); break;
        case OnlineItemAction::Drop: out.u8(0x17); out.u32(command.item); break;
        case OnlineItemAction::Take:
            if (mode == 1) { out.u8(0x1C); out.u16(command.body); }
            else { out.u8(mode == 2 ? 0x24 : 0x19); out.u32(command.item); }
            break;
        case OnlineItemAction::Place:
            out.u8(0x18); out.u32(command.item); out.u32(command.x); out.u32(command.y); out.u32(command.page); break;
        case OnlineItemAction::Equip:
            if (command.equipVariant > 2) throw ProtocolError("Unknown original equip variant");
            out.u8(command.equipVariant == 2 ? 0x1E : command.equipVariant == 1 ? 0x1B : command.targetRevision ? 0x1D : 0x1A);
            out.u32(command.item); out.u32(command.body); break;
        case OnlineItemAction::Unequip: out.u8(0x1C); out.u16(command.body); break;
        case OnlineItemAction::Swap: pair(0x1F); out.u32(command.x); out.u32(command.y); break;
        case OnlineItemAction::Use:
            out.u8(mode == 2 ? 0x26 : 0x20); out.u32(command.item);
            if (mode == 2) { out.u32(command.mercenary); out.u32(0); }
            else { out.u32(point.x); out.u32(point.y); }
            break;
        case OnlineItemAction::BeltPlace: out.u8(0x23); out.u32(command.item); out.u32(command.beltSlot); break;
        case OnlineItemAction::BeltSwap: pair(0x25); break;
        case OnlineItemAction::Stack: pair(0x21); break;
        case OnlineItemAction::Book: pair(0x29); break;
        case OnlineItemAction::Socket: pair(0x28); break;
        case OnlineItemAction::Identify:
            // Original pSpell01 prepares TARGETING before applying the target. TCP preserves order.
            if (world.itemTargetingSource != command.item) {
                Writer prepare; prepare.u8(0x20); prepare.u32(command.item); prepare.u32(point.x); prepare.u32(point.y);
                p.sent(p.gs, prepare.release());
            }
            out.u8(0x27); out.u32(command.target); out.u32(command.item); break;
        case OnlineItemAction::SwitchWeapons: out.u8(0x60); break;
        case OnlineItemAction::CubeOpen:
            p.close_interaction();
            out.u8(0x20); out.u32(command.item); out.u32(point.x); out.u32(point.y);
            world.storage.requested = OnlineStorageKind::Cube; world.storage.requestedSource = command.item;
            ++world.interactionGeneration;
            p.storageDeadline = Clock::now() + p.options.timeout; break;
        case OnlineItemAction::StorageClose:
            if (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending)
                world.itemRequest->state = OnlineItemRequest::State::Interrupted;
            p.close_interaction();
            world.itemRequest = OnlineItemRequest{++p.itemSequence, command, OnlineItemRequest::State::SentNoAck};
            p.view.error.reset(); p.changed(); return true;
        case OnlineItemAction::Transmute:
        case OnlineItemAction::GoldDeposit:
        case OnlineItemAction::GoldWithdraw:
            out.u8(0x4F); out.u16(command.action == OnlineItemAction::Transmute ? 24 : command.action == OnlineItemAction::GoldDeposit ? 20 : 19);
            out.u16(uint16_t(command.amount >> 16)); out.u16(uint16_t(command.amount)); break;
        case OnlineItemAction::GoldDrop:
            out.u8(0x50); out.u32(*p.view.load.playerUnitId); out.u32(command.amount); break;
        case OnlineItemAction::TradeOpen:
            world.shopRequested = command.npc; world.shopSource.reset();
            world.shopGamble = command.gamble;
            std::erase_if(world.items, [](const auto &entry) { return entry.second.action == 11; }); ++world.itemRevision;
            out.u8(0x38); out.u32(command.gamble ? 2 : 1); out.u32(command.npc); out.u32(0); break;
        case OnlineItemAction::Buy:
            out.u8(0x32); out.u32(command.npc); out.u32(command.item); out.u16(command.gamble ? 2 : 0); out.u16(mode); out.u32(command.amount); break;
        case OnlineItemAction::Sell:
            out.u8(0x33); out.u32(command.npc); out.u32(command.item); out.u16(mode); out.u16(0); out.u32(command.amount); break;
        case OnlineItemAction::Repair: case OnlineItemAction::RepairAll:
            out.u8(0x35); out.u32(command.npc); out.u32(command.action == OnlineItemAction::RepairAll ? UINT32_MAX : command.item);
            out.u16(0); out.u16(0); out.u32(command.action == OnlineItemAction::RepairAll ? UINT32_MAX : 0); break;
        case OnlineItemAction::IdentifyAll: out.u8(0x34); out.u32(command.npc); break;
        }
        p.sent(p.gs, out.release());
        world.itemRequest = OnlineItemRequest{++p.itemSequence, command, OnlineItemRequest::State::Pending};
        if (command.action == OnlineItemAction::Pickup) {
            world.movementRequest = OnlineMovementRequest{{}, OnlineUnitKey{4, command.item}, true, world.revision + 1, true};
        }
        p.itemDeadline = Clock::now() + p.options.timeout;
        p.nextItem = Clock::now() + std::chrono::milliseconds(100);
        ++world.revision; p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Item request could not be queued"); return false;
    }
}
bool RealmSession::submit_combat(OnlineCombatCommand command) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!command.context) command.context = onlineIntentContext(snapshot_);
    if (!p.require_intent(*command.context, false)) return false;
    auto &world = p.view.world;
    using Action = OnlineCombatCommand::Action;
    if (world.playerTrade.active() && command.action != Action::Stop) {
        p.error(OnlineErrorKind::Input, "Close the server player trade before submitting combat input"); return false;
    }
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !world.playerPosition ||
        onlinePlayerDead(world) || (command.action != Action::Stop && (p.portalRequest ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending &&
            (world.itemRequest->command.action != OnlineItemAction::Pickup || command.action != Action::Cast)) ||
        (world.combatRequest && world.combatRequest->state == OnlineCombatRequest::State::Pending)))) {
        p.error(OnlineErrorKind::Input, "Combat operation is unavailable or another operation is pending"); return false;
    }
    if (Clock::now() < p.nextCombat && command.action != Action::Stop) {
        p.error(OnlineErrorKind::Input, "Combat request is rate limited"); return false;
    }
    OnlineCombatRequest request;
    request.sequence = ++p.combatSequence; request.revision = world.revision; request.command = command;
    Writer out;
    switch (command.action) {
    case Action::BindHotkey:
        if (command.hotkeySlot >= world.skillHotkeys.size() || command.skill > 0x0FFF) {
            p.error(OnlineErrorKind::Input, "Invalid native skill hotkey"); return false;
        }
        out.u8(0x51); out.u32(uint32_t(command.hotkeySlot) << 16 | command.skill |
            (command.hand == OnlineSkillHand::Left ? 0x8000u : 0));
        out.u32(UINT32_MAX); request.state = OnlineCombatRequest::State::SentNoAck; break;
    case Action::SelectSkill:
        out.u8(0x3C); out.u32(uint32_t(command.skill) | (command.hand == OnlineSkillHand::Left ? 0x80000000u : 0));
        out.u32(UINT32_MAX); break;
    case Action::LearnSkill:
        request.before = world.playerBaseSkills.contains(command.skill) ? world.playerBaseSkills.at(command.skill) : 0;
        out.u8(0x3B); out.u16(command.skill); break;
    case Action::SpendAttribute:
        if (command.attribute > 3 || !command.count || command.count > 100) {
            p.error(OnlineErrorKind::Input, "Invalid native attribute request"); return false;
        }
        request.before = world.playerAttributes.contains(command.attribute) ? world.playerAttributes.at(command.attribute) : 0;
        out.u8(0x3A); out.u16(uint16_t(command.attribute) | uint16_t((command.count - 1) << 8)); break;
    case Action::Cast: {
        if (command.point.has_value() == command.target.has_value()) {
            p.error(OnlineErrorKind::Input, "Cast needs exactly one coordinate or assigned unit"); return false;
        }
        const auto selected = command.hand == OnlineSkillHand::Left ? world.leftSkill : world.rightSkill;
        const bool cursor = std::any_of(world.items.begin(), world.items.end(), [&](const auto &entry) {
            return entry.second.mode == 4 && entry.second.ownerType == 0 && entry.second.owner == p.view.load.playerUnitId;
        });
        if (!selected || selected->skill != command.skill || selected->owner != UINT32_MAX || cursor ||
            world.npcRequested || world.waypointSource || world.storage.kind != OnlineStorageKind::None) {
            p.error(OnlineErrorKind::Input, "Cast requires the confirmed skill and an idle player"); return false;
        }
        const bool left = command.hand == OnlineSkillHand::Left;
        if (command.target) {
            const auto found = world.units.find(*command.target);
            if (found == world.units.end() || !found->second.position) {
                p.error(OnlineErrorKind::Input, "Cast target is no longer assigned"); return false;
            }
            out.u8(left ? (command.repeat ? (command.stationary ? 0x0A : 0x09) : (command.stationary ? 0x07 : 0x06))
                        : (command.repeat ? (command.stationary ? 0x11 : 0x10) : (command.stationary ? 0x0E : 0x0D)));
            out.u32(command.target->type); out.u32(command.target->id);
        } else {
            out.u8(left ? (command.repeat ? 0x08 : 0x05) : (command.repeat ? 0x0F : 0x0C));
            out.u16(command.point->x); out.u16(command.point->y);
        }
        request.state = OnlineCombatRequest::State::SentNoAck; world.movementRequest.reset(); break;
    }
    case Action::Stop:
        out.u8(0x12); request.state = OnlineCombatRequest::State::SentNoAck; break;
    }
    try {
        p.sent(p.gs, out.release());
        if (command.action == Action::Cast) p.finish_pickup(OnlineItemRequest::State::Interrupted);
        // Releasing a channel must not erase an outstanding learn/select/spend acknowledgement.
        if (command.action != Action::Stop || !world.combatRequest ||
            world.combatRequest->state != OnlineCombatRequest::State::Pending) {
            world.combatRequest = std::move(request);
            p.combatDeadline = Clock::now() + p.options.timeout;
        }
        if (command.action != Action::Stop) p.nextCombat = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Combat request could not be queued"); return false;
    }
}
bool RealmSession::use_waypoint(uint16_t destination, uint8_t waypointNumber, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto &world = p.view.world;
    const auto waypoint = world.waypointSource ? world.waypointSource : world.waypointRequested;
    if (!waypoint || (destination && (!world.waypointSource || !world.waypointHistory)) || !world.playerPosition ||
        !p.view.load.serverLoadComplete || onlinePlayerDead(world)) {
        p.error(OnlineErrorKind::Input, "No server waypoint menu is open");
        return false;
    }
    const auto source = world.units.find({2, *waypoint});
    if (source == world.units.end() || !source->second.position || !source->second.classId ||
        (destination && (waypointNumber >= 112 ||
            !((*world.waypointHistory)[1 + waypointNumber / 16] & (1u << (waypointNumber & 15)))))) {
        p.error(OnlineErrorKind::Input, "Waypoint source or unlocked destination is unavailable");
        return false;
    }
    if (destination && Clock::now() < p.nextMovement) return false;
    try {
        // Closing must work even immediately after operate; only travel is rate limited.
        // D2MOO PlrMsg Rcv0x49: object GUID + level ID, both original DWORDs.
        Writer out;
        out.u8(0x49); out.u32(*waypoint); out.u32(destination);
        p.sent(p.gs, out.release());
        p.finish_pickup(OnlineItemRequest::State::Interrupted);
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.world.waypointSource.reset();
        p.view.world.waypointRequested.reset(); p.waypointDeadline = {};
        ++p.view.world.interactionGeneration;
        ++p.view.world.revision;
        p.view.error.reset(); p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Waypoint request could not be queued");
        return false;
    }
}
bool RealmSession::return_to_characters() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
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
    std::lock_guard lock(impl_->mutex);
    impl_->service();
    if (snapshot_.revision != impl_->view.revision) snapshot_ = impl_->view;
    impl_->snapshotDirty = false;
}
void RealmSession::cancel() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    impl_->shutdown();
    impl_->view.load = {};
    impl_->view.error.reset();
    ++impl_->view.connectionGeneration;
    impl_->view.games.clear();
    impl_->view.selectedCharacter.clear();
    impl_->stage(OnlineStage::Cancelled);
}
void RealmSession::logout() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    p.shutdown();
    const auto revision = p.view.revision, generation = p.view.connectionGeneration,
               gameGeneration = p.view.gameGeneration;
    p.view.clear();
    p.view.revision = revision + 1;
    p.view.connectionGeneration = generation + 1;
    p.view.gameGeneration = gameGeneration;
}
const OnlineView &RealmSession::read() const {
    std::lock_guard lock(impl_->mutex);
    // Refresh only on the client thread. Resource loads and render iteration
    // cannot be invalidated by a background receive.
    if (impl_->snapshotDirty) {
        snapshot_ = impl_->view;
        impl_->snapshotDirty = false;
    }
    return snapshot_;
}
std::chrono::milliseconds RealmSession::request_timeout() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->options.timeout;
}
bool RealmSession::item_request_ready() const {
    std::lock_guard lock(impl_->mutex);
    const auto &request = impl_->view.world.itemRequest;
    return impl_->view.stage == OnlineStage::ProtocolReady && impl_->current_game(snapshot_) &&
        Clock::now() >= impl_->nextItem &&
        (!request || request->state != OnlineItemRequest::State::Pending);
}
std::vector<GamePacket> RealmSession::take_game_packets() {
    std::lock_guard lock(impl_->mutex);
    auto packets = std::move(impl_->worldPackets);
    impl_->worldPackets.clear();
    impl_->worldBytes = 0;
    return packets;
}
} // namespace d2x::net

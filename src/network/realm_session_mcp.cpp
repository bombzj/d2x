#include "realm_session_impl.hpp"

namespace d2x {
namespace net {

void RealmSession::Impl::handle_mcp(Packet packet) {
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
        gameEndpoint = Endpoint{host, options.gamePort};
        gs.connect(*gameEndpoint, options.timeout);
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

void RealmSession::Impl::game_rejected(uint8_t id, uint32_t code, std::string message) {
    erase_secret(gamePassword);
    gameName.clear();
    view.gameQueuePosition.reset();
    error(OnlineErrorKind::Server, std::move(message), id, code);
    stage(OnlineStage::Lobby);
}

bool RealmSession::Impl::choose_realm(std::string name) {
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

void RealmSession::Impl::return_to_realm() {
    if (directTcpIp) {
        const bool unfinished = tcpIpDownload.incomplete();
        const bool saved = tcpIpSavedOnLeave;
        shutdown();
        view.selectedCharacter.clear();
        stage(OnlineStage::Cancelled); // Composition reconnects the local selector.
        if (unfinished || !saved)
            error(OnlineErrorKind::Transport, "TCP/IP save result unknown; local character was not overwritten with incomplete data");
        return;
    }
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

void RealmSession::Impl::reset_area(bool preserveInitialPosition) {
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
    world.npcRequested.reset(); world.npcPendingConversation.reset(); world.npcConversation.reset(); world.movementRequest.reset();
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

void RealmSession::Impl::pump_mcp() {
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
} // namespace net
} // namespace d2x

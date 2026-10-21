#include "realm_session_impl.hpp"

namespace d2x::net {

RealmSession::Impl::Impl(ClientProfile profile, bool retain)
    : compatibility(clientCompatibility(profile)), retainGamePackets(retain), gamePackets(profile) {
    view.protocolProfile = compatibility.id;
    view.remoteLobbyOnly = profile == ClientProfile::Pd2CnNormal20261020;
    view.allowLadderSelection = view.remoteLobbyOnly;
    view.gameEntryAvailable = compatibility.gameEntryBlocker.empty();
}

RealmSession::Impl::~Impl() {
    worker.request_stop();
    wake.notify_all();
    if (worker.joinable()) worker.join();
    clear_credentials();
}

void RealmSession::Impl::service() {
    try { tick(); }
    catch (const ProtocolError &e) { fail(OnlineErrorKind::Protocol, e.what()); }
    catch (const std::exception &) { fail(OnlineErrorKind::Protocol, "Network processing failed"); }
}

void RealmSession::Impl::start() {
    worker = std::jthread([this](std::stop_token stop) {
        std::unique_lock lock(mutex);
        while (!stop.stop_requested()) {
            service();
            // Timed waits release the lock; UI work never owns this worker.
            wake.wait_for(lock, std::chrono::milliseconds(10), [&] { return stop.stop_requested(); });
        }
    });
}

void RealmSession::Impl::changed() {
    ++view.revision;
}

void RealmSession::Impl::stage(OnlineStage value) {
    view.stage = value;
    if (value != OnlineStage::Lobby && value != OnlineStage::ListingGames) {
        gameInfoRequest = 0;
        view.gameInfo.reset();
    }
    deadline = Clock::now() + options.timeout;
    changed();
}

void RealmSession::Impl::clear_credentials() {
    erase_secret(options.password);
    erase_auth_secrets(options.originalClient);
    erase_secret(accountHash);
    erase_secret(loginHash);
    erase_secret(gamePassword);
    erase_secret(realmStartup);
    ticketHash = 0;
    ticketToken = 0;
    clientToken = 0;
    serverToken = 0;
}

void RealmSession::Impl::clear_game() {
    gs.close();
    gameEndpoint.reset();
    gameJoinFlags = 0;
    pd2PlayerDataBlocks = {};
    pd2PlayerUpdates = {};
    pd2RosterCounters.clear();
    pd2PlayerControl.reset(); stashPageDeadline = {};
    pd2MonstersRemaining.reset();
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
    gameStarted = {}; nextHeartbeat = {}; lastPing = {}; lastGameReceive = {};
    view.gameHeartbeat = {};
    initializedNpc.reset(); portalRequest.reset();
    environmentSent = false;
    awaitingPong = false;
    ticketHash = 0;
    ticketToken = 0;
}

void RealmSession::Impl::shutdown() {
    sid.close();
    mcp.close();
    sidPackets.reset();
    mcpPackets.reset();
    clear_game();
    clear_credentials();
    directTcpIp = false; tcpIpUploaded = tcpIpSavedOnLeave = false;
    tcpIpGameId.reset(); tcpIpUpload.clear(); tcpIpDownload.reset();
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

void RealmSession::Impl::error(OnlineErrorKind kind, std::string message, uint8_t id, uint32_t code) {
    view.error = OnlineError{kind, id, code, std::move(message), ++errorSequence};
    changed();
}

void RealmSession::Impl::fail(OnlineErrorKind kind, std::string message, uint8_t id, uint32_t code) {
    const auto heartbeat = view.gameHeartbeat;
    shutdown();
    view.gameHeartbeat = heartbeat; // Preserve the terminal transport evidence for diagnostics.
    view.load = {};
    view.games.clear();
    view.selectedCharacter.clear();
    error(kind, std::move(message), id, code);
    stage(OnlineStage::Failed);
}

bool RealmSession::Impl::require(OnlineStage value) {
    if (view.stage == value)
        return true;
    if (view.stage == OnlineStage::Failed && view.error) return false;
    error(OnlineErrorKind::Input, "Operation is unavailable in the current connection stage");
    return false;
}
bool RealmSession::Impl::require_game_entry() {
    if (compatibility.gameEntryBlocker.empty()) return true;
    error(OnlineErrorKind::Unsupported, std::string(compatibility.gameEntryBlocker));
    return false;
}

bool RealmSession::Impl::current_game(const OnlineView &published) const {
    return published.connectionGeneration == view.connectionGeneration &&
        published.gameGeneration == view.gameGeneration &&
        published.world.areaGeneration == view.world.areaGeneration &&
        published.load.playerUnitId == view.load.playerUnitId;
}

bool RealmSession::Impl::require_game(const OnlineView &published) {
    if (!require(OnlineStage::ProtocolReady)) return false;
    // A background receive can change areas while the client loads resources
    // or handles an input frame. Never send that frame to a new world/GUID.
    if (!current_game(published)) {
        error(OnlineErrorKind::Input, "The active game or area changed; refresh before submitting input");
        return false;
    }
    return true;
}

bool RealmSession::Impl::require_intent(const OnlineIntentContext &context, bool interaction) {
    if (!require(OnlineStage::ProtocolReady)) return false;
    if (!(interaction ? onlineInteractionMatches(context, view) : onlineWorldMatches(context, view))) {
        error(OnlineErrorKind::Input, "Queued intent belongs to a previous game, area or interaction");
        return false;
    }
    return true;
}

bool RealmSession::Impl::require_navigation(const OnlineIntentContext &context) {
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

void RealmSession::Impl::finish_pickup(OnlineItemRequest::State state) {
    auto &world = view.world;
    auto &request = world.itemRequest;
    if (!request || request->state != OnlineItemRequest::State::Pending ||
        request->command.action != OnlineItemAction::Pickup) return;
    request->state = state;
    if (world.movementRequest && world.movementRequest->unit == OnlineUnitKey{4, request->command.item})
        world.movementRequest.reset();
    ++world.revision;
}

void RealmSession::Impl::observed(OnlineProtocolCounters &counters, const Packet &packet, size_t header) {
    ++counters.received[packet.id];
    counters.receivedBytes += packet.body.size() + header;
    counters.lastReceived = packet.id;
    changed();
}

void RealmSession::Impl::sent(ByteStream &stream, Bytes bytes) {
    if (&stream == &gs) compatibility.validateClientPacket(bytes);
    const auto byteCount = bytes.size();
    const size_t idOffset = &stream == &gs ? 0 : &stream == &sid ? 1 : 2;
    const auto id = bytes.size() > idOffset ? std::optional<uint8_t>{bytes[idOffset]} : std::nullopt;
    Bytes observed;
    if (&stream == &gs && gameRequestObserver) observed = bytes;
    if (!stream.send(std::move(bytes)))
        throw ProtocolError("Protocol send queue unavailable");
    auto &counts = &stream == &sid ? view.sidProtocol : &stream == &mcp ? view.mcpProtocol : view.gameProtocol;
    if (id) ++counts.sent[*id];
    counts.sentBytes += byteCount;
    changed();
    if (!observed.empty()) gameRequestObserver(view.gameGeneration, observed);
}

void RealmSession::Impl::send_sid(uint8_t id, Writer out) {
    sent(sid, frame(Framing::Sid, id, out.release()));
}

void RealmSession::Impl::send_mcp(uint8_t id, Writer out) {
    sent(mcp, frame(Framing::Mcp, id, out.release()));
}

uint16_t RealmSession::Impl::next_request_id() {
    // IDs are real MCP wire IDs; no automatic wrap/reuse within one login.
    if (requestCounter == std::numeric_limits<uint16_t>::max())
        throw ProtocolError("MCP request IDs exhausted; reconnect required");
    return ++requestCounter;
}

uint16_t RealmSession::Impl::request_id() {
    pendingRequest = next_request_id();
    return pendingRequest;
}

void RealmSession::Impl::expect(OnlineStage value) const {
    if (view.stage != value)
        throw ProtocolError("Protocol reply arrived in an unexpected stage");
}

RealmSession::RealmSession(bool retainGamePackets) : RealmSession(ClientProfile::Lod113c, retainGamePackets) {}
RealmSession::RealmSession(ClientProfile profile, bool retainGamePackets)
    : impl_(std::make_unique<Impl>(profile, retainGamePackets)) {
    snapshot_ = impl_->view;
    impl_->start();
}
void RealmSession::connect_realm(std::unique_ptr<IByteTransport> realmTransport,
    std::unique_ptr<IByteTransport> gameTransport, Endpoint endpoint, std::string name, uint16_t gamePort) {
    std::lock_guard lock(impl_->mutex);
    auto &p = *impl_;
    if (p.compatibility.profile != ClientProfile::Lod113c) {
        p.snapshotDirty = true;
        p.fail(OnlineErrorKind::Unsupported, "This client profile does not support preauthenticated or embedded Realms");
        return;
    }
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
void RealmSession::connect_tcpip(Endpoint endpoint, Bytes save, std::optional<OnlineCharacter> localIdentity) {
    std::lock_guard lock(impl_->mutex);
    auto &p = *impl_;
    if (p.compatibility.profile != ClientProfile::Lod113c) {
        erase_secret(save);
        p.snapshotDirty = true;
        p.fail(OnlineErrorKind::Unsupported, "This client profile does not support TCP/IP character transfer");
        return;
    }
    if (localIdentity && (p.view.stage != OnlineStage::Idle || !character_name_valid(localIdentity->name) ||
        !localIdentity->characterClass || *localIdentity->characterClass>6))
        throw ProtocolError("Explicit TCP/IP identity requires an idle session and valid native character");
    if (!localIdentity && (p.view.stage != OnlineStage::Lobby || !p.selected || !p.selected->characterClass))
        throw ProtocolError("TCP/IP needs a selected local character");
    if (!endpoint.port || save.empty() || save.size() >= tcpIpSaveLimit || !p.tcpIpSaves.empty())
        throw ProtocolError("Invalid TCP/IP endpoint or unconsumed character save");
    auto selected = localIdentity ? std::move(*localIdentity) : *p.selected;
    p.shutdown(); p.directRealm.reset();
    p.selected = std::move(selected); p.view.selectedCharacter = p.selected->name;
    p.directTcpIp = true; p.tcpIpUpload = std::move(save);
    p.options = LoginOptions{}; p.options.gamePort = endpoint.port;
    p.gs.use(std::make_unique<TcpStream>()); p.gameEndpoint = endpoint;
    p.gs.connect(std::move(endpoint), p.options.timeout);
    p.stage(OnlineStage::ConnectingGame); p.snapshotDirty = true;
}
std::vector<Bytes> RealmSession::take_tcpip_saves() {
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->tcpIpSaves, {});
}
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
            options.originalClient.profile != p.compatibility.profile ||
            options.accountServer.host.empty() || options.timeout.count() <= 0 ||
            options.heartbeat.count() <= 0 || options.heartbeat >= options.timeout || !options.gamePort ||
            !text_valid(options.countryCode, 3) || !text_valid(options.countryName, 64))
            throw ProtocolError("Invalid login options");
        if (createAccount && (!text_valid(options.account, 15) || options.account.size() < 2 ||
                              !text_valid(options.password, 15) || options.password.size() < 2))
            throw ProtocolError("Invalid account creation fields");
        p.accountHash = account_password_hash(p.compatibility.profile, options.password);
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
        erase_auth_secrets(options.originalClient);
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
    if (p.view.remoteLobbyOnly) {
        p.error(OnlineErrorKind::Unsupported, "PD2 CN character creation flags have not been verified; select an existing character");
        return false;
    }
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
    if (p.view.remoteLobbyOnly) {
        p.error(OnlineErrorKind::Unsupported, "Character deletion is unavailable in this PD2 CN lobby profile");
        return false;
    }
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
        !found->expansion.value_or(false) || !found->ladder.has_value() ||
        (found->ladder.value_or(true) && !p.view.allowLadderSelection) ||
        (found->hardcore.value_or(true) && found->dead.value_or(true))) {
        p.error(OnlineErrorKind::Unsupported, "Select a known, playable LoD character supported by this profile");
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
        if (p.compatibility.profile == ClientProfile::Pd2CnNormal20261020)
            out.string(filter, 15);
        p.listFilter = std::move(filter); // Also filter locally; the reference PvPGN ignores wire filters.
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
    if (p.view.remoteLobbyOnly) {
        p.error(OnlineErrorKind::Unsupported, "PD2 CN game information decoding is not implemented");
        return false;
    }
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
    if (!p.require_game_entry()) {
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
        if (p.compatibility.gameCreationExtensionBytes) {
            if (!p.options.gameServer || p.options.gameServer > 10) throw ProtocolError("Invalid PD2 game server selection");
            out.u8(p.options.gameServer); out.u8(p.options.lootAllocation ? 1 : 0);
        }
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
    if (!p.require_game_entry()) {
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
void RealmSession::observe_game_packets(std::function<void(uint64_t, const protocol::Packet &)> observer) {
    std::lock_guard lock(impl_->mutex);
    impl_->gamePacketObserver = std::move(observer);
}
void RealmSession::observe_game_requests(std::function<void(uint64_t, std::span<const uint8_t>)> observer) {
    std::lock_guard lock(impl_->mutex);
    impl_->gameRequestObserver = std::move(observer);
}
} // namespace d2x::net

#include "native_realm_service.hpp"
#include "hosting/character_creation.hpp"
#include "hosting/native_game_wire.hpp"
#include "persistence/save_file.hpp"
#include "persistence/save_codec.hpp"
#include "resources/atomic_file.hpp"
#include <algorithm>
#include <random>
#include <utility>

namespace d2x::hosting {
using namespace net::protocol;
NativeRealmService::NativeRealmService(NativeRealmHost &owner, RealmOutput realm, GameOutput game)
    : shared(owner), archives(owner.archives), root(owner.root), realmOutput(std::move(realm)), gameOutput(std::move(game)),
      content(owner.content), portraits(owner.portraits), host(owner.host) {
    shared.peers.insert(this);
}
NativeRealmService::~NativeRealmService() { shared.peers.erase(this); }
void NativeRealmService::initialize() {
    shared.initialize();
    if (!store) store = std::make_unique<CharacterStore>(root, *content);
}
void NativeRealmService::sendRealm(uint8_t id, Writer out) {
    const auto entries = realmResponses();
    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto &e) { return e.id == id; });
    if (entry == entries.end() || entry->support == MessageSupport::Stub)
        throw ProtocolError("Unimplemented MCP response");
    realmOutput(id, out.release());
    ++counters.realmResponses[id];
}
void NativeRealmService::sendGame(Bytes packet) {
    validateServerPacket(packet);
    const auto id = packet[0];
    Bytes observed;
    if (shared.gamePacketObserver) observed = packet;
    gameOutput(std::move(packet));
    peer.pendingPlayerUpdate = true;
    ++counters.gameResponses[id];
    if (!observed.empty()) shared.gamePacketObserver(diagnosticConnection, GamePacketDirection::ServerToClient, observed);
}
void NativeRealmService::result(uint8_t id, uint32_t code) {
    Writer out; if (id == 0x0A) out.u16(0); out.u32(code); sendRealm(id, std::move(out));
}
const CharacterRosterEntry &NativeRealmService::find(std::string_view name) const {
    const CharacterRosterEntry *found = nullptr;
    for (const auto &entry : roster.characters) if (entry.name == name) {
        if (found) throw std::runtime_error("Ambiguous character name; repair duplicate saves");
        found = &entry;
    }
    if (!found) throw std::runtime_error("Character list changed; select again");
    return *found;
}
void NativeRealmService::checkpoint() {
    if (!binding) return;
    const auto saved = host.exportCharacter(*binding);
    if (!saved) throw std::runtime_error("Character storage binding expired or character settlement pending");
    if (externalSave) externalSave(*saved);
    else {
        if (!lease) throw std::runtime_error("Character storage binding expired");
        store->save(*lease, *saved);
    }
    if (saved->player.hardcoreDead) {
        hardcoreDeathSavedRevision = host.read(*binding)->characterRevision;
        discardReload();
    }
}
void NativeRealmService::discardReload() {
    if (reload) { shared.terrain.erase(reload->game.binding.game); host.destroy(reload->game.binding.game); }
    reload.reset();
}
void NativeRealmService::close(bool save, bool keepReload) {
    if (save) checkpoint(); // Keep the live instance and lease on save failure.
    if (binding) {
        const auto *room = shared.find(binding->game);
        if (room && room->tcpIp && room->tcpIpOwner == binding->player) {
            // The hosting process owns the TCP/IP game. Flush guests before
            // removing the owner; no remote player inherits an orphaned host.
            for (auto *guest : shared.peers) if (guest != this && guest->binding && guest->binding->game == binding->game) {
                guest->checkpoint();
            }
            for (auto *guest : shared.peers) if (guest != this && guest->binding && guest->binding->game == binding->game) {
                guest->endGame(false);
            }
        }
    }
    if (binding) shared.retire(*binding);
    binding.reset(); lease.reset(); selected.reset();
    leavePending=false;
    hardcoreDeathSavedRevision.reset();
    peer = {}; ticket = false;
    admission.clear(); terrain = {};
    gameName.clear(); gamePassword.clear();
    if (!keepReload) discardReload();
}
void NativeRealmService::endGame(bool save, bool keepReload) {
    const bool entered = peer.phase == GamePhase::Entered;
    close(save, keepReload);
    if (!entered) return;
    // Native TCP/IP B3 and B0 bypass the buffered game channel, so its wire
    // order is the returned save, B0, then the buffered 05/06. Keep 06 last:
    // the original client can release its receive buffer as the game ends.
    if (directTcpIp) sendGame({0xB0});
    sendGame({0x05});
    sendGame({0x06});
    if (!directTcpIp) sendGame({0xB0});
}
void NativeRealmService::connectTcpIp() {
    if (binding || peer.phase != GamePhase::Closed) throw ProtocolError("TCP/IP peer is already connected");
    diagnosticConnection = ++shared.nextDiagnosticConnection;
    directTcpIp = true; tcpIpUploadComplete = false; tcpIpUploadSize = 0; tcpIpUpload.reset();
    peer = {}; peer.phase = GamePhase::Connected; sendGame({0xAF, 0});
    externalSave = [this](const PersistentCharacter &character) {
        const auto bytes = encodeSave(character, *content);
        tcpIpLastSave = bytes;
        for (auto &packet : net::protocol::tcpip_download(bytes)) sendGame(std::move(packet));
    };
}
void NativeRealmService::recoverTcpIpSave() {
    if (!directTcpIp || (!binding && tcpIpLastSave.empty())) return;
    Bytes bytes = tcpIpLastSave;
    if (binding) {
        const auto saved = host.exportCharacter(*binding);
        if (!saved) throw std::runtime_error("TCP/IP recovery character is unavailable");
        bytes = encodeSave(*saved, *content);
    }
    decodeSave(bytes, *content);
    const auto directory = root / "tcpip-recovery";
    std::filesystem::create_directories(directory);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = directory / ("character-" + std::to_string(stamp) + ".d2s");
    writeFileAtomically(path, bytes, false);
    counters.lastFailure = "TCP/IP disconnected; host recovery save: " + path.string();
}
void NativeRealmService::resetRealm() {
    if (binding) close();
    authenticated = false;
}
void NativeRealmService::connectGame(bool announce) {
    // Tickets authorize exactly one connection. Never silently replace a live peer.
    if (!ticket || !binding || peer.phase != GamePhase::Closed)
        throw ProtocolError("Game connection has no unused admission ticket");
    diagnosticConnection = ++shared.nextDiagnosticConnection;
    peer = {}; peer.phase = GamePhase::Connected;
    if (announce) sendGame({0xAF, 0});
}
void NativeRealmService::failGame() {
    peer.phase = GamePhase::Closed;
    ticket = false;
    if (binding) host.enter(*binding, false);
}
void NativeRealmService::rejectAdmission(uint32_t code,std::string reason) {
    if(peer.phase==GamePhase::Entered || peer.phase==GamePhase::Closed) throw ProtocolError("Admission error outside admission");
    sendGame(encodeServerPacket(ServerMessage::AdmissionError,[&](auto &out){out.u32(code);}));
    failure=std::move(reason);counters.lastFailure=failure;++counters.failures;
    counters.lastResult="admission-rejected/"+std::to_string(code);
    failGame();
}
void NativeRealmService::setPaused(bool paused) {
    if (!binding) return;
    const auto *room = shared.find(binding->game);
    host.pause(binding->game, room && room->capacity == 1 && (paused || peer.phase != GamePhase::Entered));
}
void NativeRealmService::publishMotion(bool force) {
    if (!binding || peer.phase != GamePhase::Entered) return;
    const auto view = host.read(*binding);
    if (!view) throw std::logic_error("Active game binding expired");
    if (!force && view->revision == peer.sentRevision) return;
    if (view->movementSequence != peer.sentMovement && view->command.sequence == view->movementSequence &&
        view->command.status != CommandStatus::Applied && view->command.status != CommandStatus::Queued)
        force = true; // A rejected movement still needs an authoritative correction.
    const auto resources=nativePlayerResources(*view,terrain.origin);
    const auto &old=peer.resources;
    uint8_t resourceMessage=0;
    // Game::sub_6FC39030 gates PlrMsg::sub_6FC82360. Its four idle
    // checks are resource checks, NOT four simulation frames. Evaluating the
    // inner condition at 25 Hz hid stale-coordinate clients on D2X hosts.
    const bool urgentLife = old && view->attributes.maxLife > 0 &&
        100 * std::abs(int(old->life)-int(resources.life)) / view->attributes.maxLife >= 10 &&
        (resources.life != 0 || old->life == 0);
    const bool check = force || !old || peer.resourceArea != view->actor.region || urgentLife ||
        view->tick >= peer.resourceCheckTick + 20 ||
        (peer.pendingPlayerUpdate && view->tick >= peer.resourceCheckTick + 10);
    if (check) {
        peer.resourceCheckTick = view->tick;
        if(!old || old->potionLife!=resources.potionLife || old->potionMana!=resources.potionMana) resourceMessage=0x18;
        else if(old->life!=resources.life || old->mana!=resources.mana) resourceMessage=0x95;
        else if(force || peer.resourceArea!=view->actor.region || old->stamina!=resources.stamina) resourceMessage=0x96;
        else if (peer.resourceInactivity > 3 &&
            (std::abs(int(old->x)-resources.x)>=2 || std::abs(int(old->y)-resources.y)>=2)) resourceMessage=0x96;
        if (resourceMessage) peer.resourceInactivity = 0;
        else if (peer.resourceInactivity < 4) ++peer.resourceInactivity;
    }
    std::vector<Bytes> packets;
    if(resourceMessage) packets.push_back(nativeResourceUpdate(resources,resourceMessage));
    // Locomotion is predicted by the original owner client. Death and forced
    // moving skill coordinates retain their separate original action packets.
    auto packet = view->life<=0 || ((view->attacking || view->recovering) && view->actor.moving)
        ? nativePlayerMotion(*view, terrain.origin) : Bytes{};
    // Tick revisions alone must not broadcast identical idle poses at 25 Hz.
    // A processed/rejected move still needs its authoritative correction.
    if(!packet.empty() && (force || packet!=peer.lastMotion || view->movementSequence!=peer.sentMovement)) packets.push_back(packet);
    if(!packets.empty()) sendGameBatch(std::move(packets));
    if(resourceMessage) {peer.resources=resources;peer.resourceArea=view->actor.region;}
    peer.pendingPlayerUpdate = false;
    peer.lastMotion=std::move(packet);
    peer.sentRevision = view->revision; peer.sentMovement = view->movementSequence;
}
void NativeRealmService::prepareReload() {
    if (!binding || !lease || peer.phase != GamePhase::Entered)
        throw std::runtime_error("No loaded character to reload");
    if (reload) throw std::runtime_error("A character reload is already pending");
    const auto live = host.read(*binding);
    if (live && live->life <= 0) {
        const auto record = host.exportCharacter(*binding, false);
        if (!record || record->player.hardcore) throw std::runtime_error("Cannot reload a dead hardcore character");
    }
    if (const auto *room = shared.find(binding->game); room && room->capacity != 1)
        throw std::runtime_error("Reload is available only in a private one-player instance");
    auto saved = store->load(*lease);
    saved.difficulty = int(gameDifficulty);
    // Allocate the version/name metadata before acquiring a staged host slot.
    ReloadCandidate candidate{};
    candidate.expectedFile=lease->expected;
    candidate.name=selectedName;
    candidate.difficulty=gameDifficulty;
    candidate.game = prepareGame(std::move(saved));
    reload = std::move(candidate);
}
NativeRealmService::PreparedGame NativeRealmService::prepareGame(PersistentCharacter saved,std::optional<server::GameSettings> settings) {
    auto prepared = prepareWalkingGame(archives, *content, std::move(saved), shared.items,std::move(settings));
    const auto staged = host.create(std::move(prepared.authority));
    try {
        auto packets = nativeGameAdmission(*content, *host.exportCharacter(staged), prepared.terrain, *host.read(staged), host.area(staged.game, RegionId(prepared.terrain.request.level))->definition, *host.questProjection(staged),host.environment(staged.game,RegionId(prepared.terrain.request.level)).value(),host.objectSeed(staged.game).value());
        for (const auto &packet : packets) validateServerPacket(packet);
        host.pause(staged.game, true);
        shared.terrain[staged.game].emplace(RegionId(prepared.terrain.request.level), prepared.terrain);
        return {staged, std::move(prepared.terrain), std::move(packets)};
    } catch (...) { shared.terrain.erase(staged.game); host.destroy(staged.game); throw; }
}
std::string NativeRealmService::prepareStartup(const std::string &load, const std::string &save, const std::string &characterClass) {
    auto &o = *this;
    if (!shared.games.empty() || shared.peers.size() > 1) throw std::runtime_error("Cannot change the save repository while other host sessions exist");
    o.close(); o.initialize();
    const auto destination = std::filesystem::absolute(!save.empty() ? std::filesystem::path(save)
        : !load.empty() ? std::filesystem::path(load) : root / "quick.d2s").lexically_normal();
    o.root = destination.parent_path(); o.store = std::make_unique<CharacterStore>(o.root, *o.content);
    PersistentCharacter character;
    if (!load.empty()) character = loadSave(load, *o.content);
    else {
        const auto name = characterClass.empty() ? "Barbarian" : characterClass;
        const auto found = std::find_if(o.content->characters.begin(), o.content->characters.end(), [&](const auto &c) { return c.name == name; });
        if (found == o.content->characters.end()) throw std::runtime_error("Unknown MPQ character class");
        character = d2x::createCharacter(*o.content, "Hero", unsigned(found - o.content->characters.begin()), uint32_t(std::random_device{}()));
    }
    const bool sameFile = !load.empty() && std::filesystem::exists(destination) && std::filesystem::equivalent(load, destination);
    if (!sameFile) o.store->install(destination.filename().string(), character);
    o.startupFile = destination.filename().string();
    return character.player.name;
}

}

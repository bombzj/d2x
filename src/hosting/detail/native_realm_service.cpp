#include "native_realm_service.hpp"
#include "hosting/character_creation.hpp"
#include "hosting/character_rules.hpp"
#include "hosting/native_game_wire.hpp"
#include "persistence/save_file.hpp"
#include <algorithm>
#include <random>
#include <utility>

namespace d2x::hosting {
using namespace net::protocol;
NativeRealmService::NativeRealmService(NativeRealmHost &owner, RealmOutput realm, GameOutput game)
    : shared(owner), archives(owner.archives), root(owner.root), realmOutput(std::move(realm)), gameOutput(std::move(game)),
      content(owner.content), portraits(owner.portraits), host(owner.host), rules(owner.rules) {
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
    gameOutput(std::move(packet));
    ++counters.gameResponses[id];
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
    if (!saved || !lease) throw std::runtime_error("Character storage binding expired");
    store->save(*lease, *saved);
}
void NativeRealmService::discardReload() {
    if (reload) { shared.terrain.erase(reload->game.binding.game); host.destroy(reload->game.binding.game); }
    reload.reset();
}
void NativeRealmService::close(bool save, bool keepReload) {
    if (save) checkpoint(); // Keep the live instance and lease on save failure.
    if (binding) shared.retire(*binding);
    binding.reset(); lease.reset(); selected.reset();
    peer = {}; ticket = false;
    admission.clear(); terrain = {};
    gameName.clear(); gamePassword.clear();
    if (!keepReload) discardReload();
}
void NativeRealmService::resetRealm() {
    if (binding) close();
    authenticated = false;
}
void NativeRealmService::connectGame(bool announce) {
    // Tickets authorize exactly one connection. Never silently replace a live peer.
    if (!ticket || !binding || peer.phase != GamePhase::Closed)
        throw ProtocolError("Game connection has no unused admission ticket");
    peer = {}; peer.phase = GamePhase::Connected;
    if (announce) sendGame({0xAF, 0});
}
void NativeRealmService::failGame() {
    peer.phase = GamePhase::Closed;
    ticket = false;
    if (binding) host.enter(*binding, false);
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
    auto packet = nativePlayerMotion(*view, terrain.origin);
    if (packet.empty()) { peer.lastMotion.clear(); return; }
    // Tick revisions alone must not broadcast identical idle poses at 25 Hz.
    // A processed/rejected move still needs its authoritative correction.
    if (force || packet != peer.lastMotion || view->movementSequence != peer.sentMovement) {
        sendGame(packet); peer.lastMotion = std::move(packet);
    }
    peer.sentRevision = view->revision; peer.sentMovement = view->movementSequence;
}
void NativeRealmService::prepareReload() {
    if (!binding || !lease || peer.phase != GamePhase::Entered)
        throw std::runtime_error("No loaded character to reload");
    if (reload) throw std::runtime_error("A character reload is already pending");
    if (const auto *room = shared.find(binding->game); room && room->capacity != 1)
        throw std::runtime_error("Reload is available only in a private one-player instance");
    auto saved = store->load(*lease);
    saved.difficulty = int(gameDifficulty);
    // Allocate the version/name metadata before acquiring a staged host slot.
    ReloadCandidate candidate{{}, lease->expected, selectedName, gameDifficulty};
    candidate.game = prepareGame(std::move(saved));
    reload = std::move(candidate);
}
NativeRealmService::PreparedGame NativeRealmService::prepareGame(PersistentCharacter saved) {
    auto prepared = prepareWalkingGame(archives, *content, std::move(saved), rules, shared.items);
    const auto staged = host.create(std::move(prepared.authority));
    try {
        auto packets = nativeGameAdmission(*content, *host.exportCharacter(staged), prepared.terrain, *host.read(staged), host.area(staged.game, RegionId(prepared.terrain.request.level))->definition);
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
    const auto destination = std::filesystem::absolute(!save.empty() ? save : !load.empty() ? load : "saves/quick.d2s").lexically_normal();
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

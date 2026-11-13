#include "gameplay/character/persistent_character.hpp"
#include "server/game_messages.hpp"
#include "server/area_store.hpp"
#include "server/systems/world/system.hpp"
#include "native_realm_service.hpp"
#include "hosting/native_game_wire.hpp"
#include "content/world/world_catalog.hpp"
#include <algorithm>
#include <random>
#include <cctype>
namespace d2x::hosting {
using namespace net::protocol;
namespace {
std::string gameKey(std::string value) { for (auto &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return value; }
void eligible(const CharacterRosterEntry &character, const HostedGame &room) {
    if (!character.level) throw std::runtime_error("Character level is unavailable");
    const auto progression = (character.nativeStatus >> 8) & 31;
    const auto unlocked = unsigned(progression >= 10 ? 2 : progression >= 5 ? 1 : 0);
    if (unsigned(room.settings.difficulty) > unlocked || room.hardcore != bool(character.nativeStatus & 4) ||
        std::abs(int(*character.level) - int(room.creatorLevel)) > room.levelDifference)
        throw std::runtime_error("Character does not meet room eligibility");
}
void issueTicket(NativeRealmService &peer) {
    peer.ticket = false;
    // Credentials are unique among pending admissions, independent of GUIDs.
    do { peer.hash = uint32_t(std::random_device{}()); peer.token = uint16_t(std::random_device{}()); }
    while (!peer.hash || !peer.token || peer.shared.ticket(peer.hash, peer.token));
    peer.ticket = true;
}
}
NativeRealmService::PreparedGame NativeRealmService::prepareGuest(PersistentCharacter saved, const HostedGame &room) {
    if (host.participants(room.handle).size() >= room.capacity) throw AdmissionFailure(15, "Game is full");
    const auto town = saved.lastRegion;
    const auto catalog = sharedWorldCatalog(archives, room.settings.difficulty);
    const auto &level = catalog->level(int(town));
    if (!level.town) throw ProtocolError("Joining character lacks a native entry town");
    auto request = host.requestAdmissionTown(room.handle, town);
    if (!request) throw ProtocolError("Unable to request the joining character's town");
    while (*request.value) {
        const auto pending=host.pendingAreas(room.handle);
        if(!pending) throw ProtocolError("Joining town preparation expired");
        const auto ticket=std::find_if(pending->begin(),pending->end(),[&](const auto &entry){return entry.request==*request.value;});
        if(ticket==pending->end()) throw ProtocolError("Joining town preparation ticket is missing");
        shared.prepareAreaNow(room.handle,*ticket);
        // A town may already have several immutable reload tickets in flight.
        // Complete all missing inputs before admitting this synchronous guest.
        request=host.requestAdmissionTown(room.handle,town);
        if(!request) throw ProtocolError("Unable to finish the joining character's town");
    }
    // Installing a new town allocates world IDs. Remap the incoming D2S afterwards.
    auto definition = prepareGameCharacter(*content, std::move(saved), room.settings, host.nextEntity(room.handle), shared.items);
    const auto staged = host.admit(room.handle, std::move(definition));
    try { return prepareAdmission(staged, shared.terrain.at(room.handle).at(town)); }
    catch (...) { host.remove(staged); throw; }
}
void NativeRealmService::admitExternal(PersistentCharacter saved, HostedGame &room, uint32_t ticketHash, uint16_t gameId) {
    if (binding || (!externalSave && !externalCheckpoint) || !gameId) throw ProtocolError("Invalid external admission");
    if (saved.player.hardcoreDead) throw AdmissionFailure(21,"Hardcore character is dead");
    if (saved.player.hardcore != room.hardcore) throw AdmissionFailure(room.hardcore?19:20,"Character hardcore mode does not match game");
    selectedName = saved.player.name;
    selected = saved;
    gameDifficulty = unsigned(room.settings.difficulty);
    gameLadder = (room.flags & 0x00200000u) != 0;
    gameName = room.name; gamePassword = room.password;
    auto prepared = room.handle.generation ? prepareGuest(std::move(saved), room) : prepareGame(std::move(saved), room.settings);
    room.handle = prepared.binding.game;
    binding = prepared.binding; terrain = std::move(prepared.terrain); admission = std::move(prepared.admission);
    multiplayerEndpoint = true;
    hash = ticketHash; token = gameId; ticket = true;
    host.pause(binding->game, false);
}
void NativeRealmService::createGame(net::protocol::Reader &in) {
    const auto request = in.u16(); const auto flags = in.u32(); const auto templateId = in.u8(), levelDifference = in.u8(), players = in.u8();
    const auto name = in.string(15), password = in.string(15), description = in.string(31); in.finish();
    uint32_t code = 0;
    std::optional<PlayerBinding> stagedBinding;
    try {
        if (!selected || !lease || binding || !players || players > 8) throw std::runtime_error("Game requires a selected character and 1-8 player slots");
        if ((flags & ~0x3004u) || !(flags & 4) || templateId != 1 || levelDifference > 99 || name.empty())
            throw std::runtime_error("Unsupported native game creation options");
        const auto key = gameKey(name);
        if (shared.games.contains(key) || shared.games.size() >= 256 || shared.nextGameIndex == UINT32_MAX)
            throw std::runtime_error("Room name or directory capacity unavailable");
        gameDifficulty = (flags >> 12) & 3;
        HostedGame room; room.name = name; room.password = password; room.description = description;
        room.tcpIp = tcpIpHost;
        if (room.tcpIp && std::any_of(shared.games.begin(), shared.games.end(), [](const auto &entry) { return entry.second.tcpIp; }))
            throw std::runtime_error("A TCP/IP host can expose only one game");
        room.flags = flags; room.capacity = players; room.levelDifference = levelDifference;
        room.creatorLevel = uint8_t(selected->player.level); room.hardcore = bool(find(selectedName).nativeStatus & 4);
        if (room.hardcore) room.flags |= 0x800;
        room.settings = {selected->mapSeed, int(gameDifficulty), !multiplayerEndpoint && players == 1};
        room.settings.hardcore = room.hardcore;
        room.settings.nativeGameType=room.tcpIp?2:1;
        eligible(find(selectedName), room);
        PreparedGame prepared;
        if (reload) {
            if (players != 1 || reload->name != selectedName || reload->difficulty != gameDifficulty || reload->expectedFile != lease->expected)
                throw std::runtime_error("Prepared reload no longer matches this admission");
            prepared = std::move(reload->game); reload.reset();
        } else { prepared = prepareGame(store->load(*lease, room.settings.difficulty), room.settings); }
        stagedBinding = prepared.binding;
        room.handle = prepared.binding.game; room.index = shared.nextGameIndex;
        room.tcpIpOwner = prepared.binding.player;
        try { shared.games.emplace(key, room); }
        catch (...) { shared.terrain.erase(room.handle); host.destroy(room.handle); throw; }
        ++shared.nextGameIndex;
        binding = prepared.binding; terrain = std::move(prepared.terrain); admission = std::move(prepared.admission);
        gameName = name; gamePassword = password; issueTicket(*this);
        host.pause(binding->game, players == 1);
    } catch (const std::exception &error) {
        failure = error.what(); code = 0x1F;
        if (stagedBinding) { shared.retire(*stagedBinding); binding.reset(); ticket = false; admission.clear(); terrain = {}; }
    }
    Writer out; out.u16(request); out.u16(0); out.u16(0); out.u32(code); sendRealm(3, std::move(out));
}
void NativeRealmService::joinGame(net::protocol::Reader &in) {
    const auto request = in.u16(); const auto name = in.string(15), password = in.string(15); in.finish();
    uint32_t code = 0;
    std::optional<PlayerBinding> stagedBinding;
    try {
        const auto found = shared.games.find(gameKey(name));
        if (found == shared.games.end() || found->second.password != password || !selected || !lease) throw std::runtime_error("Room or password unavailable");
        const auto &room = found->second;
        eligible(find(selectedName), room);
        if (binding) {
            if (!ticket || binding->game != room.handle) throw std::runtime_error("Character already admitted elsewhere");
        } else {
            auto prepared = prepareGuest(store->load(*lease, room.settings.difficulty), room);
            stagedBinding = prepared.binding; binding = prepared.binding;
            terrain = std::move(prepared.terrain); admission = std::move(prepared.admission);
            gameDifficulty = unsigned(room.settings.difficulty); gameName = room.name; gamePassword = room.password;
            issueTicket(*this);
        }
    } catch (const std::exception &error) {
        failure = error.what(); code = 0x2A;
        if (stagedBinding) { shared.retire(*stagedBinding); binding.reset(); ticket = false; admission.clear(); terrain = {}; }
    }
    Writer out; out.u16(request); out.u16(code ? 0 : token); out.u16(0);
    for (const auto byte : gameAddress) out.u8(byte);
    out.u32(code ? 0 : hash); out.u32(code); sendRealm(4, std::move(out));
}
void NativeRealmService::listGames(net::protocol::Reader &in) {
    const auto request = in.u16(); in.u32(); in.finish();
    for (const auto &[key, room] : shared.games) {
        (void)key;
        if (!selected || room.hardcore != selected->player.hardcore) continue;
        Writer out; out.u16(request); out.u32(room.index); out.u8(uint8_t(host.participants(room.handle).size()));
        out.u32(room.flags); out.string(room.name); out.string(room.description); sendRealm(5, std::move(out));
    }
    Writer out; out.u16(request); out.u32(0); out.u8(0); out.u32(0); out.string(""); out.string(""); sendRealm(5, std::move(out));
}
void NativeRealmService::gameInfo(net::protocol::Reader &in) {
    const auto request = in.u16(); const auto name = in.string(15); in.finish();
    const auto found = shared.games.find(gameKey(name));
    // MCP has no verified missing-info result packet; absent names time out.
    if (found == shared.games.end()) return;
    const auto &room = found->second;
    std::vector<PersistentCharacter> characters;
    for (const auto id : host.participants(room.handle)) if(const auto record=host.exportCharacter({room.handle,id},false)) characters.push_back(*record);
    Writer out; out.u16(request); out.u32(room.flags);
    out.u32(uint32_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - room.created).count()));
    out.u8(room.creatorLevel); out.u8(room.levelDifference); out.u8(room.capacity); out.u8(uint8_t(characters.size()));
    for (size_t i = 0; i < 16; ++i) {
        uint8_t value = 0;
        if (i < characters.size()) {
            const auto entry = std::find_if(content->characters.begin(), content->characters.end(), [&](const auto &c) { return c.name == characters[i].player.characterClass; });
            value = uint8_t(entry - content->characters.begin());
        }
        out.u8(value);
    }
    for (size_t i = 0; i < 16; ++i) out.u8(i < characters.size() ? uint8_t(characters[i].player.level) : 0);
    out.string(room.description);
    for (const auto &character : characters) out.string(character.player.name);
    sendRealm(6, std::move(out));
}
}

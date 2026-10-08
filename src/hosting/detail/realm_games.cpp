#include "native_realm_service.hpp"
#include "hosting/native_game_wire.hpp"
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
        room.flags = flags; room.capacity = players; room.levelDifference = levelDifference;
        room.creatorLevel = uint8_t(selected->player.level); room.hardcore = bool(find(selectedName).nativeStatus & 4);
        room.settings = {selected->mapSeed, int(gameDifficulty), !multiplayerEndpoint && players == 1};
        eligible(find(selectedName), room);
        PreparedGame prepared;
        if (reload) {
            if (players != 1 || reload->name != selectedName || reload->difficulty != gameDifficulty || reload->expectedFile != lease->expected)
                throw std::runtime_error("Prepared reload no longer matches this admission");
            prepared = std::move(reload->game); reload.reset();
        } else { auto saved = *selected; saved.difficulty = int(gameDifficulty); prepared = prepareGame(std::move(saved), room.settings.singlePlayer); }
        stagedBinding = prepared.binding;
        room.handle = prepared.binding.game; room.town = RegionId(prepared.terrain.request.level); room.index = shared.nextGameIndex;
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
            if (host.participants(room.handle).size() >= room.capacity) throw std::runtime_error("Room is full");
            const auto &area = shared.terrain.at(room.handle).at(room.town);
            auto definition = prepareJoiningCharacter(*content, *selected, room.settings, room.town, host.nextEntity(room.handle), rules, shared.items);
            const auto state = host.area(room.handle, room.town);
            for (auto &corpse : definition.persistent.corpses) { corpse.region = room.town; corpse.position = state->definition.spawn; }
            const auto staged = host.admit(room.handle, std::move(definition));
            stagedBinding = staged;
            try {
                auto packets = nativeGameAdmission(*content, *host.exportCharacter(staged), area, *host.read(staged), state->definition);
                for (const auto &packet : packets) validateServerPacket(packet);
                terrain = area; admission = std::move(packets); binding = staged;
            } catch (...) { host.remove(staged); throw; }
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
    for (const auto id : host.participants(room.handle)) characters.push_back(*host.exportCharacter({room.handle, id}));
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

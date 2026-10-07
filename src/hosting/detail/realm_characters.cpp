#include "native_realm_service.hpp"
#include "hosting/character_creation.hpp"
#include <random>

namespace d2x::hosting {
using namespace net::protocol;
void NativeRealmService::listCharacters(net::protocol::Reader &in) {
    const auto requested = in.u32(); in.finish();
    if (binding) throw ProtocolError("Character is already in game");
    lease.reset(); selected.reset(); selectedName.clear(); ticket = false;
    roster = store->list(!startupFile.empty(), startupFile);
    std::vector<const CharacterRosterEntry *> visible;
    counters.characterIssues.clear();
    for (const auto &entry : roster.characters) {
        if (!validCharacterName(entry.name)) {
            if (counters.characterIssues.size() < 1024)
                counters.characterIssues.push_back(entry.fileName + ": name cannot be represented by native MCP");
            continue;
        }
        visible.push_back(&entry);
        if (!entry.problem.empty() && counters.characterIssues.size() < 1024)
            counters.characterIssues.push_back(entry.fileName + ": " + entry.problem);
    }
    if (visible.size() != roster.characters.size())
        failure = "Some save names cannot be represented by MCP; see server-status characterIssues";
    const auto count = std::min<size_t>({visible.size(), requested, 1024});
    Writer out; out.u16(uint16_t(std::min<uint32_t>(requested, 65535))); out.u32(uint32_t(visible.size())); out.u16(uint16_t(count));
    for (size_t i = 0; i < count; ++i) {
        const auto &c = *visible[i];
        out.u32(0); out.string(c.name, 15);
        Bytes portrait;
        if (c.playable && c.characterClass && c.level && c.appearance)
            portrait = portraits->encode(*c.appearance, *c.characterClass, *c.level, c.nativeStatus);
        out.append(portrait); out.u8(0);
    }
    sendRealm(0x19, std::move(out));
}
void NativeRealmService::createCharacter(net::protocol::Reader &in) {
    const auto characterClass = in.u16(); in.u16(); const auto flags = in.u16(); const auto name = in.string(15); in.finish();
    try {
        if (binding || flags != 0x20) throw std::runtime_error("Only non-hardcore expansion saves are supported");
        store->create(name, characterClass, uint32_t(std::random_device{}())); result(2, 0);
    } catch (const std::exception &e) { failure = e.what(); result(2, 0x16); }
}
void NativeRealmService::deleteCharacter(net::protocol::Reader &in) {
    in.u16(); const auto name = in.string(15); in.finish();
    try { if (binding || lease) throw std::runtime_error("Character is in use"); const auto id = find(name).id; store->erase(roster.revision, id); result(0x0A, 0); }
    catch (const std::exception &e) { failure = e.what(); result(0x0A, 0x49); }
}
void NativeRealmService::selectCharacter(net::protocol::Reader &in) {
    const auto name = in.string(15); in.finish();
    try {
        if (binding) throw std::runtime_error("A game is already active");
        lease.reset(); selected.reset();
        if (reload && reload->name != name) discardReload();
        const auto &entry = find(name);
        if (!entry.playable) throw std::runtime_error(entry.problem);
        auto nextLease = store->acquire(roster.revision, entry.id);
        auto saved = store->load(*nextLease);
        lease = std::move(nextLease); selected = std::move(saved); selectedName = name;
        result(7, 0);
    } catch (const std::exception &e) { failure = e.what(); result(7, 0x46); }
}
}

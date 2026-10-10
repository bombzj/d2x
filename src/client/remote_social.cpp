#include "remote_social.hpp"
#include <algorithm>
#include <chrono>

namespace d2x::net {
namespace {
using namespace protocol;
OnlineRosterPlayer &player(OnlineSocialView &social, uint32_t id) {
    if (id == UINT32_MAX) throw ProtocolError("Invalid roster player ID");
    if (!social.players.contains(id) && social.players.size() >= 64)
        throw ProtocolError("Remote roster limit exceeded");
    auto &entry = social.players[id];
    entry.id = id;
    entry.revision = social.revision;
    return entry;
}
std::string fixedName(Reader &r) {
    const auto bytes = r.take(16);
    const auto end = std::find(bytes.begin(), bytes.end(), uint8_t{});
    if (end == bytes.end()) throw ProtocolError("Unterminated roster player name");
    if (end == bytes.begin() || !std::all_of(bytes.begin(), end, [](uint8_t c) { return c >= 32 && c < 127; }))
        throw ProtocolError("Invalid native roster player name");
    return {bytes.begin(), end};
}
}
bool apply_social_packet(OnlineView &v, const protocol::Packet &packet) {
    switch (packet.id) {
    case 0x26: case 0x5A: case 0x5B: case 0x5C: case 0x75: case 0x76: case 0x7F: case 0x8B: case 0x8C: case 0x8D: case 0x90:
        break;
    default: return false;
    }
    auto &social = v.world.social;
    ++social.revision;
    Reader r(packet.body);
    switch (packet.id) {
    case 0x76: {
        const auto type=r.u8();const auto id=r.u32();r.finish();if(type==0) social.hover.erase(id);break;
    }
    case 0x5A: {
        OnlineSocialView::Notice notice; notice.type=r.u8();notice.color=r.u8();notice.value=r.u32();notice.parameter=r.u8();
        const auto names=r.take(32);notice.names.assign(names.begin(),names.end());r.finish();
        notice.sequence=++social.chatSequence;
        notice.receivedMilliseconds=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if(const auto identity=social.players.find(notice.value);identity!=social.players.end()) notice.playerName=identity->second.name;
        social.notices.push_back(std::move(notice));if(social.notices.size()>256) social.notices.pop_front();break;
    }
    case 0x26: {
        OnlineChatMessage message;
        message.type = r.u8(); message.language = r.u8(); message.unitType = r.u8();
        message.unitId = r.u32(); message.messageColor = r.u8(); message.nameColor = r.u8();
        const auto sender = r.string(15), text = r.string(255);
        r.finish();
        message.name.assign(sender.begin(), sender.end()); message.text.assign(text.begin(), text.end());
        message.sequence = ++social.chatSequence;
        message.receivedMilliseconds = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if(message.type==5 && message.unitType==0) social.hover.insert_or_assign(message.unitId,message);
        social.chat.push_back(std::move(message));
        if (social.chat.size() > 256) social.chat.pop_front();
        break;
    }
    case 0x5B: {
        if (r.u16() != packet.body.size() + 1) throw ProtocolError("Invalid roster packet length");
        auto &entry = player(social, r.u32());
        entry.characterClass = r.u8();
        entry.name = fixedName(r);
        entry.level = r.u16(); entry.partyId = r.u16();
        entry.rosterUnknown = r.u16(); // Unverified field at 0x1C, never a permission.
        entry.partyFlags = r.u16(); entry.guildFlags = r.u16();
        const auto extension = r.take(r.remaining());
        entry.extension.assign(extension.begin(), extension.end());
        entry.listed = true;
        break;
    }
    case 0x5C: {
        const auto id = r.u32(); r.finish();
        social.players.erase(id);
        social.hover.erase(id);
        std::erase_if(social.relationships, [&](const auto &entry) { return entry.first.first == id || entry.first.second == id; });
        // Spatial removal is consumed by remote_world; it retains corpse ownership.
        break;
    }
    case 0x75: {
        auto &entry = player(social, r.u32());
        entry.partyId = r.u16(); entry.level = r.u16(); entry.relationshipFlags = r.u16();
        entry.partyStatus = r.u16(); r.finish();
        // SCmd::0x75 uses the same PlayerList field_C as 0x8B. A newer
        // roster update must replace an older invitation state.
        entry.partyState = *entry.partyStatus <= UINT8_MAX
            ? std::optional<uint8_t>{uint8_t(*entry.partyStatus)} : std::nullopt;
        break;
    }
    case 0x7F: {
        const auto isPlayer = r.u8(); const auto life = r.u16(); const auto id = r.u32(); const auto area = r.u16();
        r.finish();
        if (isPlayer > 1) throw ProtocolError("Invalid ally information unit kind");
        if (isPlayer) { auto &entry = player(social, id); entry.lifePercentage = life; entry.area = area; }
        // The other branch is a hireling update; its consumer belongs to that domain.
        else { ++v.gameProtocol.unconsumed[packet.id]; ++v.world.ignoredPackets; }
        break;
    }
    case 0x8B: {
        auto &entry = player(social, r.u32()); entry.partyState = r.u8(); r.finish();
        break;
    }
    case 0x8C: {
        const auto first = r.u32(), second = r.u32(); const auto flags = r.u16(); r.finish();
        if (social.relationships.size() >= 4096 && !social.relationships.contains({first, second}))
            throw ProtocolError("Remote relation limit exceeded");
        social.relationships[{first, second}] = flags;
        break;
    }
    case 0x8D: {
        auto &entry = player(social, r.u32()); entry.partyId = r.u16(); r.finish();
        break;
    }
    case 0x90: {
        auto &entry = player(social, r.u32()); entry.positionX = r.u32(); entry.positionY = r.u32(); r.finish();
        break;
    }
    }
    return true;
}
}

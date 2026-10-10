#pragma once
#include "core/bytes.hpp"
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace d2x {
// Roster membership and public party data are independent of spatial assignment.
// An update arriving before 0x5B retains unknown identity; it creates no world unit.
struct OnlineRosterPlayer {
    uint32_t id{};
    uint64_t revision{};
    bool listed{};
    std::string name;
    std::optional<uint8_t> characterClass, partyState;
    std::optional<uint16_t> level, partyId, partyFlags, guildFlags;
    std::optional<uint16_t> rosterUnknown, relationshipFlags, partyStatus;
    std::optional<uint16_t> area, lifePercentage;
    std::optional<uint32_t> positionX, positionY; // Public 0x90 coordinates, no spatial unit.
    Bytes extension; // Uninterpreted 0x5B guild metadata, not a display label.
};
struct OnlineChatMessage {
    uint64_t sequence{}, receivedMilliseconds{};
    uint8_t type{}, language{}, unitType{}, messageColor{}, nameColor{};
    // Native byte 0x09 is called nNameColor in D2PacketDef, but PlrMsg sets it
    // to the sender's level for a player broadcast. Do not treat it as a palette
    // index or use unitId (normally 0, unitType=2) as the sender's player GUID.
    uint32_t unitId{};
    Bytes name, text; // Native language bytes; UI must decode before display.
};
struct OnlineSocialView {
    struct Notice { uint8_t type{},color{},parameter{}; uint32_t value{}; Bytes names; uint64_t sequence{},receivedMilliseconds{};std::string playerName; };
    std::deque<Notice> notices;
    uint64_t revision{}, chatSequence{};
    std::map<uint32_t, OnlineRosterPlayer> players;
    // Directed native relation flags; absence does not mean friendly or hostile.
    std::map<std::pair<uint32_t, uint32_t>, uint16_t> relationships;
    std::deque<OnlineChatMessage> chat;
    std::map<uint32_t,OnlineChatMessage> hover;
};
// Native 0x5E commands; 0x8B/0x75 provide the receiver-relative invitation state.
enum class OnlinePartyAction : uint8_t { Invite = 6, Cancel = 7, Accept = 8, Leave = 9 };
inline std::optional<OnlinePartyAction> onlinePartyAction(const OnlineRosterPlayer &player) {
    if (!player.listed) return {};
    const auto state = player.partyState ? std::optional<uint16_t>{*player.partyState} : player.partyStatus;
    if (!state) return {};
    switch (*state) {
    case 0: return OnlinePartyAction::Invite;
    case 2: return OnlinePartyAction::Accept;
    case 4: return OnlinePartyAction::Cancel;
    default: return {};
    }
}
}

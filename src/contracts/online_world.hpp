#pragma once
#include "core/bytes.hpp"
#include "contracts/online_items.hpp"
#include <compare>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <set>

namespace d2x {
struct OnlinePoint {
    uint16_t x{}, y{};
    bool operator==(const OnlinePoint &) const = default;
};
struct OnlineUnitKey {
    uint8_t type{};
    uint32_t id{};
    auto operator<=>(const OnlineUnitKey &) const = default;
};
struct OnlineSkillSelection {
    uint16_t skill{};
    uint32_t owner{UINT32_MAX};
    bool operator==(const OnlineSkillSelection &) const = default;
};
enum class OnlineSkillHand { Left, Right };
struct OnlineCombatCommand {
    enum class Action { SelectSkill, Cast, Stop, LearnSkill, SpendAttribute };
    Action action{};
    OnlineSkillHand hand{OnlineSkillHand::Right};
    uint16_t skill{};
    uint8_t attribute{}, count{1};
    std::optional<OnlinePoint> point;
    std::optional<OnlineUnitKey> target;
    bool stationary{}, repeat{};
};
struct OnlineCombatRequest {
    enum class State { Pending, Confirmed, TimedOut, Interrupted, SentNoAck };
    uint64_t sequence{}, revision{};
    OnlineCombatCommand command;
    State state{State::Pending};
    uint32_t before{};
};
struct OnlineCombatEvent {
    enum class Kind { Skill, Hit, Action, Overlay, Missile };
    uint64_t sequence{};
    uint8_t packet{};
    Kind kind{};
    OnlineUnitKey source;
    std::optional<OnlineUnitKey> target;
    std::optional<OnlinePoint> point;
    std::optional<std::array<uint32_t, 2>> missileDestination; // Native first path point, not spawn origin.
    std::optional<uint8_t> pierce;
    std::optional<uint16_t> skill, level, overlay, missile;
    std::optional<uint8_t> action, hitClass, life, direction;
    uint32_t flags{}, auxiliary{};
};
struct OnlineStateMessage {
    enum class Kind { Snapshot, Enable, Disable };
    uint64_t sequence{};
    Kind kind{};
    uint8_t state{};
    Bytes packed;
};
struct OnlineUnit {
    OnlineUnitKey key;
    std::optional<uint16_t> classId;
    std::optional<OnlinePoint> position, destination;
    std::optional<OnlineUnitKey> destinationUnit;
    std::optional<uint8_t> mode, lifePercent; // Life ratio byte preserved in its original wire scale.
    bool lifeCarriesRankFlag{}; // Only monster hit updates reserve bit 7 for rank.
    std::optional<uint8_t> portalFlags, portalDestination;
    std::optional<uint32_t> portalOwner;
    std::string portalOwnerName;
    std::string name;
    Bytes appearanceBits; // NPC assignment tail; decoded with current MPQ component tables.
    uint64_t positionRevision{}, appearanceRevision{}, positionDiscontinuity{};
    bool equipmentObserved{};
    std::optional<uint8_t> wireAction, hitClass;
    std::optional<uint8_t> direction; // Native 0..63 path facing.
    std::optional<uint16_t> actionSkill, actionSkillLevel;
    bool nativeMode{}; // 0x0E carries an actual mode, unlike player action packets.
    uint64_t actionRevision{}, hitRevision{};
    std::optional<uint8_t> pathType, pathSteps, pathDistance;
    std::optional<int16_t> velocityPercent;
    // State stat widths belong to MPQ consumers; the transport retains ordered native messages.
    uint64_t stateSequence{};
    std::deque<OnlineStateMessage> stateMessages;
    // Keep early skill packets by server GUID until 0x0B identifies the local player.
    std::map<uint16_t, uint16_t> skills;
    std::map<uint16_t, uint8_t> baseSkills, bonusSkills, itemSkillQuantities;
    std::optional<OnlineSkillSelection> leftSkill, rightSkill;
};
struct OnlineMovementRequest {
    std::optional<OnlinePoint> destination;
    std::optional<OnlineUnitKey> unit;
    bool run{}; // Requested gait, not a simulated server position or mode.
    uint64_t revision{};
};
struct OnlineNpcMessage {
    uint16_t stringId{};
    uint8_t menu{}; // Native TEXT: 0 automatic, 1 topic, 2 both.
};
struct OnlineNpcConversation {
    uint32_t source{};
    uint64_t revision{};
    std::vector<OnlineNpcMessage> messages;
    Bytes questFlags;
    std::set<uint16_t> acknowledged; // Enqueued 0x31 requests; never quest completion.
};
struct OnlineEquippedItem {
    uint32_t id{}, owner{};
    uint32_t flags{};
    uint8_t bodyLocation{}, component{};
    std::string code;
    std::optional<uint8_t> quality;
    bool autoAffix{};
};
struct OnlineMapEvent {
    enum class Kind { RevealRoom, HideRoom, PlayerPosition, RemovePlayer };
    uint64_t sequence{};
    Kind kind{};
    uint8_t level{}; // Only room events have a level; position never guesses one.
    OnlinePoint point{}; // Room tiles or player subtiles, according to kind.
};
enum class OnlineDeathPhase { Unknown, Alive, Dying, Dead };
struct OnlineRespawnRequest {
    enum class State { WaitingForDeath, Sent, Confirmed, TimedOut };
    State state{State::WaitingForDeath};
    uint64_t revision{};
    bool sent{};
    uint8_t restoredResources{};
    bool repositioned{};
};
struct OnlineWorldView {
    OnlineDeathPhase deathPhase{OnlineDeathPhase::Unknown};
    uint64_t deathRevision{};
    std::optional<OnlineRespawnRequest> respawnRequest;
    std::map<uint32_t, uint32_t> corpseOwners; // Original 0x8E corpse GUID -> player GUID.
    uint64_t revision{}, areaGeneration{};
    std::map<OnlineUnitKey, OnlineUnit> units;
    // Room anchors are in tiles, unit coordinates in subtiles (five per tile).
    // The packet does not contain room extents.
    std::map<std::tuple<uint8_t, uint16_t, uint16_t>, OnlinePoint> rooms;
    // First 0x07 receipt for each currently assigned room. This preserves wire
    // ordering; it is not proof of server-side DRLG activation or generation order.
    std::map<std::tuple<uint8_t, uint16_t, uint16_t>, uint64_t> roomAssignmentRevisions;
    // Ordered within areaGeneration. Consumers must reject a missing prefix,
    // rather than rebuild native RNG/activation from the final room set.
    uint64_t mapEventSequence{};
    std::deque<OnlineMapEvent> mapEvents;
    std::optional<OnlinePoint> mapInitialPlayerPosition; // Preserved early LOADACT assignment.
    std::map<uint32_t, OnlineEquippedItem> equipment;
    std::map<uint32_t, OnlineItem> items;
    uint64_t itemRevision{};
    std::optional<OnlineItemRequest> itemRequest;
    OnlineStorageContext storage;
    std::optional<uint32_t> shopRequested, shopSource;
    std::optional<OnlineTradeResult> tradeResult;
    unsigned weaponSet{}; // Original 0x97 toggles the client weapon inventory.
    std::map<uint8_t, uint32_t> playerAttributes;
    std::optional<OnlinePoint> playerPosition;
    std::optional<uint16_t> life, mana, stamina;
    std::optional<OnlineMovementRequest> movementRequest;
    std::optional<uint32_t> npcRequested;
    std::optional<OnlineNpcConversation> npcConversation;
    std::map<uint16_t, uint16_t> playerSkills;
    std::map<uint16_t, uint8_t> playerBaseSkills, playerBonusSkills, itemSkillQuantities;
    std::optional<OnlineSkillSelection> leftSkill, rightSkill;
    uint64_t combatSequence{};
    std::deque<OnlineCombatEvent> combatEvents;
    std::optional<OnlineCombatRequest> combatRequest;
    bool townPortalPending{}; // Enqueued command, not proof that a portal exists.
    std::optional<std::array<uint16_t, 8>> waypointHistory; // Native 0x102 header + 112 bits.
    std::optional<uint32_t> waypointSource; // Only 0x63 authorizes an open menu.
    uint64_t ignoredPackets{};
};
inline bool onlinePlayerDead(const OnlineWorldView &world) {
    if (world.deathPhase != OnlineDeathPhase::Unknown)
        return world.deathPhase == OnlineDeathPhase::Dying || world.deathPhase == OnlineDeathPhase::Dead;
    return world.life && !*world.life;
}
} // namespace d2x

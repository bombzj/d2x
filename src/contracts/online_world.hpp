#pragma once
#include "core/bytes.hpp"
#include "contracts/online_items.hpp"
#include "contracts/online_social.hpp"
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
enum class OnlineObjectIntent { Operate, Stash, Waypoint };
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
struct OnlineSkillHotkey {
    std::optional<OnlineSkillSelection> selection;
    OnlineSkillHand hand{OnlineSkillHand::Right};
};
struct OnlineQuestState {
    std::optional<std::array<uint16_t, 48>> playerFlags, gameFlags;
    std::array<std::optional<uint8_t>, 41> statuses;
    std::map<uint8_t, std::array<uint16_t, 3>> updates; // Quest number -> flags, status, progress (0x5D).
    std::optional<uint16_t> denRemaining, rescuedBarbsRemaining, staffTombOffset;
    uint64_t revision{};
};
struct OnlineCombatCommand {
    enum class Action { SelectSkill, Cast, Stop, LearnSkill, SpendAttribute, BindHotkey };
    Action action{};
    OnlineSkillHand hand{OnlineSkillHand::Right};
    uint16_t skill{};
    uint8_t attribute{}, count{1};
    uint8_t hotkeySlot{};
    std::optional<OnlinePoint> point;
    std::optional<OnlineUnitKey> target;
    bool stationary{}, repeat{};
    // MPQ-derived UI expectation for a remote skill interaction; never a wire field.
    std::optional<OnlineObjectIntent> interaction;
    std::optional<OnlineIntentContext> context;
};
struct OnlineCombatRequest {
    enum class State { Pending, Confirmed, TimedOut, Interrupted, SentNoAck };
    uint64_t sequence{}, revision{};
    OnlineCombatCommand command;
    State state{State::Pending};
    uint32_t before{};
};
struct OnlineCombatEvent {
    enum class Kind { Skill, Hit, Action, Overlay, Missile, Sound };
    uint64_t sequence{}, receivedMilliseconds{}; // Client monotonic receipt time, never a wire field.
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
    // 0x18/0x95/0x96: current position minus signed target offsets.
    // This is the server path's reachable tTargetCoord, not a new movement action.
    std::optional<OnlinePoint> verifiedDestination;
    uint64_t pathVerificationRevision{};
    std::optional<OnlineUnitKey> destinationUnit;
    std::optional<uint8_t> mode, lifePercent; // Life ratio byte preserved in its original wire scale.
    bool lifeCarriesRankFlag{}; // Only monster hit updates reserve bit 7 for rank.
    std::optional<uint8_t> portalFlags, portalDestination;
    std::optional<uint8_t> objectInteractType; // 0x51 ObjectData.InteractType; MPQ consumers interpret it.
    std::optional<bool> objectTargetable; // Object 0x0E flag update; absent on initial assignment.
    std::optional<uint32_t> portalOwner;
    std::string portalOwnerName;
    std::string name;
    Bytes appearanceBits; // NPC assignment tail; decoded with current MPQ component tables.
    uint64_t positionRevision{}, appearanceRevision{}, positionDiscontinuity{};
    uint64_t assignmentRevision{}; // A new spatial assignment invalidates presentation cached before removal.
    bool equipmentObserved{};
    std::map<uint8_t, int32_t> attributes; // 1.13c 0x20: public player stats, separate from this client's private stats.
    std::optional<uint8_t> wireAction, hitClass;
    std::optional<uint8_t> direction; // Native 0..63 path facing.
    std::optional<uint16_t> actionSkill, actionSkillLevel;
    bool nativeMode{}; // 0x0E carries an actual mode, unlike player action packets.
    uint64_t actionRevision{}, hitRevision{};
    uint64_t actionReceivedMilliseconds{}; // Local receipt time, not server action time.
    std::optional<uint8_t> pathType, pathSteps, pathDistance;
    std::optional<int16_t> velocityPercent;
    // State stat widths belong to MPQ consumers; the transport retains ordered native messages.
    uint64_t stateSequence{};
    std::deque<OnlineStateMessage> stateMessages;
    // Keep early skill packets by server GUID until 0x0B identifies the local player.
    std::map<uint16_t, uint16_t> skills;
    std::map<uint16_t, uint8_t> baseSkills, bonusSkills, itemSkillQuantities;
    bool baseSkillsAssigned = false; // Complete original 0x94 list, including an empty list.
    std::optional<OnlineSkillSelection> leftSkill, rightSkill;
};
struct OnlineMovementRequest {
    std::optional<OnlinePoint> destination;
    std::optional<OnlineUnitKey> unit;
    bool run{}; // Requested gait, not a simulated server position or mode.
    uint64_t revision{};
    bool interaction{};
};
inline constexpr int onlineMovementProgressTimeoutSeconds = 15;
struct OnlineNpcMessage {
    uint16_t stringId{};
    uint8_t menu{}; // Native TEXT discriminator: 1.13c type 0 automatic, type 2 selectable topic.
};
struct OnlineNpcConversation {
    uint32_t source{};
    uint64_t revision{};
    std::vector<OnlineNpcMessage> messages;
    std::set<uint16_t> acknowledged; // Enqueued 0x31 requests; never quest completion.
};
struct OnlineEquippedItem {
    uint32_t id{}, owner{};
    uint32_t flags{};
    uint8_t bodyLocation{}, component{}, ownerType{};
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
// Native 0x77 authorizes the invitation/open state. The other player's identity
// is unknown until 0x78 arrives after acceptance; never guess it from proximity.
enum class OnlinePlayerTradeAction { Agree, Revoke, Gold };
struct OnlinePlayerTrade {
    enum class Phase { None, Outgoing, Incoming, Open };
    enum class Response { None, AcceptSent, CancelSent, TimedOut, GoldSent, ResetSent };
    Phase phase{Phase::None};
    Response response{Response::None};
    uint64_t revision{};
    std::optional<uint8_t> lastAction;
    std::optional<uint32_t> peer;
    std::string peerName;
    uint32_t ownGold{}, peerGold{};
    bool ownAgreed{}, peerAgreed{}, agreementLocked{};
    bool active() const { return phase != Phase::None; }
};
struct OnlineRespawnRequest {
    enum class State { WaitingForDeath, Sent, Confirmed, TimedOut };
    State state{State::WaitingForDeath};
    uint64_t revision{};
    bool sent{};
    uint8_t restoredResources{};
    bool repositioned{};
};
struct OnlinePet { uint8_t type{}; uint16_t monsterClass{}; uint32_t owner{}; };
struct OnlineWorldView {
    OnlineSocialView social;
    OnlinePlayerTrade playerTrade;
    OnlineDeathPhase deathPhase{OnlineDeathPhase::Unknown};
    uint64_t deathRevision{};
    std::optional<OnlineRespawnRequest> respawnRequest;
    std::map<uint32_t, OnlinePet> pets; // Original 0x7A ownership, independent of unit visibility.
    std::map<uint32_t, uint32_t> corpseOwners; // Original 0x8E corpse GUID -> player GUID.
    uint64_t revision{}, areaGeneration{}, interactionGeneration{};
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
    uint64_t itemTargetingRevision{};
    std::optional<uint32_t> itemTargetingSource; // Native 0x3F preparation, not identification success.
    OnlineStorageContext storage;
    std::optional<uint32_t> shopRequested, shopSource;
    bool shopGamble{};
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
    bool playerBaseSkillsAssigned = false;
    std::optional<OnlineSkillSelection> leftSkill, rightSkill;
    std::array<std::optional<OnlineSkillHotkey>, 16> skillHotkeys; // Native 0x7B, unknown until received.
    OnlineQuestState quests;
    std::set<OnlineUnitKey> questAlerts; // Original 0x8A hints, not locally inferred quest eligibility.
    uint64_t combatSequence{};
    std::deque<OnlineCombatEvent> combatEvents;
    std::optional<OnlineCombatRequest> combatRequest;
    bool townPortalPending{}; // Enqueued command, not proof that a portal exists.
    std::optional<std::array<uint16_t, 8>> waypointHistory; // Native 0x102 header + 112 bits.
    std::optional<uint32_t> waypointSource; // Only 0x63 authorizes an open menu.
    std::optional<uint32_t> waypointRequested; // Local 0x13 intent, never an open-menu confirmation.
    uint64_t lateWaypointReplies{};
    uint64_t ignoredPackets{};

    void clear() {
        playerTrade = {};
        social.revision = social.chatSequence = 0;
        social.players.clear();
        social.relationships.clear();
        social.chat.clear();
        deathPhase = OnlineDeathPhase::Unknown;
        deathRevision = 0;
        respawnRequest.reset();
        corpseOwners.clear();
        pets.clear();
        revision = areaGeneration = interactionGeneration = 0;
        units.clear();
        rooms.clear();
        roomAssignmentRevisions.clear();
        mapEventSequence = 0;
        mapEvents.clear();
        mapInitialPlayerPosition.reset();
        equipment.clear();
        items.clear();
        itemRevision = 0;
        itemRequest.reset();
        itemTargetingSource.reset();itemTargetingRevision=0;
        storage = {};
        shopRequested.reset();
        shopSource.reset();
        shopGamble = false;
        tradeResult.reset();
        weaponSet = 0;
        playerAttributes.clear();
        playerPosition.reset();
        life.reset();
        mana.reset();
        stamina.reset();
        movementRequest.reset();
        npcRequested.reset();
        npcConversation.reset();
        playerSkills.clear();
        playerBaseSkills.clear();
        playerBonusSkills.clear();
        itemSkillQuantities.clear();
        playerBaseSkillsAssigned = false;
        leftSkill.reset();
        rightSkill.reset();
        skillHotkeys = {};
        quests.playerFlags.reset();
        quests.gameFlags.reset();
        for (auto &status : quests.statuses) status.reset();
        quests.updates.clear();
        quests.denRemaining.reset();
        quests.rescuedBarbsRemaining.reset();
        quests.staffTombOffset.reset();
        quests.revision = 0;
        questAlerts.clear();
        combatSequence = 0;
        combatEvents.clear();
        combatRequest.reset();
        townPortalPending = false;
        waypointHistory.reset();
        waypointSource.reset();
        waypointRequested.reset();
        lateWaypointReplies = ignoredPackets = 0;
    }
};
inline bool onlineMonsterCorpse(const OnlineUnit &unit) {
    return unit.key.type == 1 && (unit.mode == 0 || unit.mode == 12 ||
        (unit.lifePercent && (unit.lifeCarriesRankFlag ? (*unit.lifePercent & 0x7f) : *unit.lifePercent) == 0));
}
inline bool onlinePlayerDead(const OnlineWorldView &world) {
    if (world.deathPhase != OnlineDeathPhase::Unknown)
        return world.deathPhase == OnlineDeathPhase::Dying || world.deathPhase == OnlineDeathPhase::Dead;
    return world.life && !*world.life;
}
} // namespace d2x

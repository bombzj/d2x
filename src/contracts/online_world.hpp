#pragma once
#include "core/bytes.hpp"
#include <compare>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <tuple>

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
struct OnlineUnit {
    OnlineUnitKey key;
    std::optional<uint16_t> classId;
    std::optional<OnlinePoint> position, destination;
    std::optional<uint8_t> mode, lifePercent; // Life ratio byte preserved in its original wire scale.
    std::optional<uint8_t> portalFlags, portalDestination;
    std::optional<uint32_t> portalOwner;
    std::string portalOwnerName;
    std::string name;
    Bytes appearanceBits; // NPC assignment tail; decoded with current MPQ component tables.
    uint64_t positionRevision{}, appearanceRevision{};
    bool equipmentObserved{};
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
struct OnlineWorldView {
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
    std::map<uint8_t, uint32_t> playerAttributes;
    std::optional<OnlinePoint> playerPosition;
    std::optional<uint16_t> life, mana, stamina;
    std::optional<std::array<uint16_t, 8>> waypointHistory; // Native 0x102 header + 112 bits.
    std::optional<uint32_t> waypointSource; // Only 0x63 authorizes an open menu.
    uint64_t ignoredPackets{};
};
} // namespace d2x

#pragma once
#include "core/bytes.hpp"
#include <compare>
#include <cstdint>
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
struct OnlineWorldView {
    uint64_t revision{}, areaGeneration{};
    std::map<OnlineUnitKey, OnlineUnit> units;
    // Room anchors are in tiles, unit coordinates in subtiles (five per tile).
    // The packet does not contain room extents.
    std::map<std::tuple<uint8_t, uint16_t, uint16_t>, OnlinePoint> rooms;
    std::map<uint32_t, OnlineEquippedItem> equipment;
    std::map<uint8_t, uint32_t> playerAttributes;
    std::optional<OnlinePoint> playerPosition;
    std::optional<uint16_t> life, mana, stamina;
    uint64_t ignoredPackets{};
};
} // namespace d2x

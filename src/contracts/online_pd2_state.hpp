#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace d2x {
struct OnlinePd2State {
    struct EffectTimer {
        std::array<uint16_t, 4> parameters{};
        uint8_t flags{};
        uint32_t value{};
    };
    struct Hotkey {
        uint16_t skill{}, auxiliary{};
        uint32_t itemOwner{};
    };
    struct MapAttributeReport {
        struct Attribute {
            uint16_t parameter{}, stat{};
            int32_t value{};
        };
        uint32_t headerFirst{}, headerSecond{}, itemIndex{};
        uint16_t area{};
        uint8_t quality{};
        std::vector<Attribute> attributes;
    };
    std::optional<MapAttributeReport> mapAttributes;
    std::optional<std::array<uint16_t, 196>> materialSnapshot;
    std::map<uint16_t, uint16_t> materialSlotUpdates;
    bool materialMappingAvailable{};
    std::map<uint16_t, uint16_t> unmappedMaterialUpdates;
    std::array<uint8_t, 386> discovery{};
    bool discoverySnapshotKnown{};
    std::map<uint16_t, EffectTimer> effectTimers;
    std::map<uint16_t, uint8_t> playerValues;
    std::optional<uint8_t> defaultPlayerValue;
    std::optional<uint32_t> mapInfoIndex, appearancePermissions, lootAllocation;
    std::array<std::optional<Hotkey>, 16> hotkeys;
    std::optional<std::pair<uint8_t, uint32_t>> monstersRemaining;
    uint64_t revision{}, materialSnapshots{}, materialUpdates{}, discoverySnapshots{}, discoveryUpdates{};

    std::optional<uint16_t> materialCount(uint16_t slot) const {
        if (slot >= 196) return {};
        if (const auto found = materialSlotUpdates.find(slot); found != materialSlotUpdates.end()) return found->second;
        if (materialSnapshot) return (*materialSnapshot)[slot];
        return {};
    }

    std::optional<bool> discovered(uint8_t quality, uint16_t index) const {
        if (!((quality == 7 && index < 2048) || (quality == 5 && index < 1024))) return {};
        const std::size_t offset = (quality == 7 ? 2 : 258) + index / 8;
        if (discovery[offset] & (1u << (index % 8))) return true;
        if (discoverySnapshotKnown) return false;
        return {};
    }
};
} // namespace d2x
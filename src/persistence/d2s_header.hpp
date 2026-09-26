#pragma once
#include "core/bytes.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace d2x {
inline constexpr size_t d2sHeaderSize = 0x14F;
inline constexpr uint32_t d2sVersion = 96;
struct D2sHeader {
    std::string name;
    uint32_t weaponSet = 0;
    uint32_t flags = 0x20;
    uint32_t created = 0, saved = 0;
    std::array<uint32_t, 16> hotkeys{};
    std::array<uint32_t, 4> selectedSkills{};
    std::array<uint8_t, 16> appearance{}, colors{};
    std::array<uint8_t, 3> towns{0x80, 0, 0};
    uint32_t mercFlags = 0, mercSeed = 0, mercExperience = 0;
    uint16_t mercName = 0, mercType = 0;
    uint32_t lastLevel = 1, lastTown = 1;
    uint8_t characterClass = 0;
    uint8_t level = 1;
    uint8_t skillCount = 30;
    uint8_t difficulty = 0;
    uint32_t mapSeed = 0;
};
D2sHeader readD2sHeader(std::span<const uint8_t> bytes);
void writeD2sHeader(Bytes &bytes, const D2sHeader &header);
} // namespace d2x
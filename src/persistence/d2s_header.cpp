#include "d2s_header.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
uint32_t get32(std::span<const uint8_t> bytes, size_t offset) {
    return uint32_t(bytes[offset]) | uint32_t(bytes[offset + 1]) << 8 |
           uint32_t(bytes[offset + 2]) << 16 | uint32_t(bytes[offset + 3]) << 24;
}
void put32(Bytes &bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index)
        bytes[offset + index] = uint8_t(value >> (8 * index));
}
uint32_t checksum(std::span<const uint8_t> bytes) {
    uint32_t result = 0;
    for (size_t index = 0; index < bytes.size(); ++index) {
        const auto value = index >= 0x0C && index < 0x10 ? 0 : bytes[index];
        result = (result << 1) | (result >> 31);
        result += value;
    }
    return result;
}
void validate(const D2sHeader &header) {
    if (header.name.empty() || header.name.size() > 15 ||
        !std::all_of(header.name.begin(), header.name.end(), [](unsigned char letter) {
            return (letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') ||
                   (letter >= '0' && letter <= '9') || letter == '-';
        }) || header.weaponSet > 1 ||
        !(header.flags & 0x20) || header.characterClass > 6 || !header.skillCount ||
        header.level == 0 || header.level > 99 || header.difficulty > 2)
        throw std::runtime_error("Invalid Diablo II expansion character header fields");
}
} // namespace
D2sHeader readD2sHeader(std::span<const uint8_t> bytes) {
    if (bytes.size() < d2sHeaderSize || get32(bytes, 0) != 0xAA55AA55 ||
        get32(bytes, 4) != d2sVersion || get32(bytes, 8) != bytes.size() ||
        get32(bytes, 12) != checksum(bytes))
        throw std::runtime_error("Invalid Diablo II expansion save header or checksum");
    D2sHeader header;
    auto name = bytes.subspan(0x14, 16);
    auto end = std::find(name.begin(), name.end(), uint8_t{0});
    if (end == name.end()) throw std::runtime_error("Unterminated Diablo II character name");
    header.name.assign(name.begin(), end);
    header.weaponSet = get32(bytes, 0x10);
    header.flags = get32(bytes, 0x24);
    header.created = get32(bytes, 0x2C);
    header.saved = get32(bytes, 0x30);
    for (size_t index = 0; index < 16; ++index) {
        header.hotkeys[index] = get32(bytes, 0x38 + index * 4);
        header.appearance[index] = bytes[0x88 + index];
        header.colors[index] = bytes[0x98 + index];
    }
    for (size_t index = 0; index < 4; ++index)
        header.selectedSkills[index] = get32(bytes, 0x78 + index * 4);
    std::copy_n(bytes.begin() + 0xA8, 3, header.towns.begin());
    header.mercFlags = get32(bytes, 0xAF);
    header.mercSeed = get32(bytes, 0xB3);
    header.mercName = uint16_t(bytes[0xB7] | uint16_t(bytes[0xB8]) << 8);
    header.mercType = uint16_t(bytes[0xB9] | uint16_t(bytes[0xBA]) << 8);
    header.mercExperience = get32(bytes, 0xBB);
    header.lastLevel = get32(bytes, 0xD0);
    header.lastTown = get32(bytes, 0xD4);
    header.characterClass = bytes[0x28];
    header.skillCount = bytes[0x2A];
    header.level = bytes[0x2B];
    header.mapSeed = get32(bytes, 0xAB);
    header.difficulty = bytes[0xD8];
    validate(header);
    return header;
}
void writeD2sHeader(Bytes &bytes, const D2sHeader &header) {
    if (bytes.size() < d2sHeaderSize || bytes.size() > UINT32_MAX)
        throw std::runtime_error("Invalid Diablo II header buffer size");
    validate(header);
    put32(bytes, 0, 0xAA55AA55);
    put32(bytes, 4, d2sVersion);
    put32(bytes, 8, uint32_t(bytes.size()));
    put32(bytes, 12, 0);
    put32(bytes, 0x10, header.weaponSet);
    std::fill_n(bytes.begin() + 0x14, 16, 0);
    std::copy(header.name.begin(), header.name.end(), bytes.begin() + 0x14);
    put32(bytes, 0x24, header.flags);
    bytes[0x29] = 16;
    put32(bytes, 0x2C, header.created);
    put32(bytes, 0x30, header.saved);
    put32(bytes, 0x34, UINT32_MAX);
    for (size_t index = 0; index < 16; ++index) {
        put32(bytes, 0x38 + index * 4, header.hotkeys[index]);
        bytes[0x88 + index] = header.appearance[index];
        bytes[0x98 + index] = header.colors[index];
    }
    for (size_t index = 0; index < 4; ++index)
        put32(bytes, 0x78 + index * 4, header.selectedSkills[index]);
    std::copy(header.towns.begin(), header.towns.end(), bytes.begin() + 0xA8);
    put32(bytes, 0xAF, header.mercFlags);
    put32(bytes, 0xB3, header.mercSeed);
    bytes[0xB7] = uint8_t(header.mercName);
    bytes[0xB8] = uint8_t(header.mercName >> 8);
    bytes[0xB9] = uint8_t(header.mercType);
    bytes[0xBA] = uint8_t(header.mercType >> 8);
    put32(bytes, 0xBB, header.mercExperience);
    put32(bytes, 0xD0, header.lastLevel);
    put32(bytes, 0xD4, header.lastTown);
    bytes[0x28] = header.characterClass;
    bytes[0x2A] = header.skillCount;
    bytes[0x2B] = header.level;
    put32(bytes, 0xAB, header.mapSeed);
    bytes[0xD8] = header.difficulty;
    put32(bytes, 12, checksum(bytes));
}
} // namespace d2x
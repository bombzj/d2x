#include "d2s_fixed_sections.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
void validate(const D2sFixedSections &sections) {
    const auto &quests = sections.quests;
    if (!std::equal(quests.begin(), quests.begin() + 4, "Woo!") ||
        quests[4] != 6 || quests[5] != 0 || quests[8] != 0x2A || quests[9] != 1)
        throw std::runtime_error("Invalid Diablo II quest section");
    const auto &waypoints = sections.waypoints;
    if (waypoints[0] != 'W' || waypoints[1] != 'S' ||
        waypoints[6] != waypoints.size() || waypoints[7] != 0)
        throw std::runtime_error("Invalid Diablo II waypoint section");
    const auto &introductions = sections.introductions;
    if (introductions[0] != 1 || introductions[1] != 0x77 ||
        introductions[2] != introductions.size() || introductions[3] != 0)
        throw std::runtime_error("Invalid Diablo II introduction section");
}
} // namespace
D2sFixedSections readD2sFixedSections(std::span<const uint8_t> bytes) {
    constexpr size_t fixedSize = 298 + 80 + 52;
    if (bytes.size() < d2sHeaderSize + fixedSize)
        throw std::runtime_error("Truncated Diablo II character sections");
    D2sFixedSections sections;
    auto cursor = bytes.begin() + d2sHeaderSize;
    std::copy_n(cursor, sections.quests.size(), sections.quests.begin());
    cursor += sections.quests.size();
    std::copy_n(cursor, sections.waypoints.size(), sections.waypoints.begin());
    cursor += sections.waypoints.size();
    std::copy_n(cursor, sections.introductions.size(), sections.introductions.begin());
    validate(sections);
    return sections;
}
void writeD2sFixedSections(Bytes &bytes, const D2sFixedSections &sections) {
    if (bytes.size() != d2sHeaderSize)
        throw std::runtime_error("Diablo II sections must follow the save header");
    validate(sections);
    bytes.insert(bytes.end(), sections.quests.begin(), sections.quests.end());
    bytes.insert(bytes.end(), sections.waypoints.begin(), sections.waypoints.end());
    bytes.insert(bytes.end(), sections.introductions.begin(), sections.introductions.end());
}
} // namespace d2x
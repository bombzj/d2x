// First-act DS1 object lookup facts, from OpenDiablo2 object_lookup_record_data.go.
// See docs/resources/THIRD_PARTY.md for source and license.
#pragma once
#include <array>
#include <span>
namespace d2x {
struct Preset {
    int type, id;
    const char *category;
    const char *token;
    const char *mode;
    const char *weapon;
    std::array<const char *, 16> gear;
};
std::span<const Preset> act1ObjectPresets();
} // namespace d2x

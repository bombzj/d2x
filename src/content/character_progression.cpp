#include "character_progression.hpp"
#include <charconv>
#include <cstddef>
#include <stdexcept>

namespace d2x {
std::vector<uint64_t> barbarianExperienceThresholds(const DataTable &experience) {
    if (!experience.has("Level") || !experience.has("Barbarian"))
        throw std::runtime_error("MPQ Experience.txt lacks Barbarian level thresholds");
    int maxLevel = 0;
    for (size_t row = 0; row < experience.rows().size(); ++row)
        if (experience.value(row, "Level") == "MaxLvl")
            maxLevel = experience.number(row, "Barbarian").value_or(0);
    if (maxLevel < 2 || maxLevel > 255)
        throw std::runtime_error("Invalid MPQ Barbarian maximum level");

    // Row N is the original threshold for advancing from level N to N+1.
    std::vector<uint64_t> thresholds{0, 0};
    for (size_t row = 0; row < experience.rows().size(); ++row) {
        if (experience.value(row, "Level") == "MaxLvl") continue;
        auto level = experience.number(row, "Level");
        if (!level || *level < 1 || *level >= maxLevel) continue;
        auto value = experience.value(row, "Barbarian");
        uint64_t threshold = 0;
        auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), threshold);
        if (value.empty() || error != std::errc{} || end != value.data() + value.size() ||
            size_t(*level + 1) != thresholds.size() || threshold <= thresholds.back())
            throw std::runtime_error("Invalid MPQ Barbarian experience threshold");
        thresholds.push_back(threshold);
    }
    if (thresholds.size() != size_t(maxLevel + 1))
        throw std::runtime_error("MPQ Barbarian experience progression is incomplete");
    return thresholds;
}
} // namespace d2x

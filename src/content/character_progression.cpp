#include "character_progression.hpp"
#include <charconv>
#include <cstddef>
#include <stdexcept>

namespace d2x {
std::vector<uint64_t> experienceThresholds(const DataTable &experience, std::string_view character) {
    if (!experience.has("Level") || !experience.has(character))
        throw std::runtime_error("MPQ Experience.txt lacks level thresholds for " + std::string(character));
    int maxLevel = 0;
    for (size_t row = 0; row < experience.rows().size(); ++row)
        if (experience.value(row, "Level") == "MaxLvl")
            maxLevel = experience.number(row, character).value_or(0);
    if (maxLevel < 2 || maxLevel > 255)
        throw std::runtime_error("Invalid MPQ maximum level for " + std::string(character));

    // Row N is the original threshold for advancing from level N to N+1.
    std::vector<uint64_t> thresholds{0, 0};
    for (size_t row = 0; row < experience.rows().size(); ++row) {
        if (experience.value(row, "Level") == "MaxLvl") continue;
        auto level = experience.number(row, "Level");
        if (!level || *level < 1 || *level >= maxLevel) continue;
        auto value = experience.value(row, character);
        uint64_t threshold = 0;
        auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), threshold);
        if (value.empty() || error != std::errc{} || end != value.data() + value.size() ||
            size_t(*level + 1) != thresholds.size() || threshold <= thresholds.back())
            throw std::runtime_error("Invalid MPQ experience threshold for " + std::string(character));
        thresholds.push_back(threshold);
    }
    if (thresholds.size() != size_t(maxLevel + 1))
        throw std::runtime_error("MPQ experience progression is incomplete for " + std::string(character));
    return thresholds;
}
} // namespace d2x

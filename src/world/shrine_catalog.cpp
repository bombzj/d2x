#include "shrine_catalog.hpp"
#include "generation_seed.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
int number(const std::map<std::string, std::string> &row, const char *key) {
    auto found = row.find(key);
    return found == row.end() || found->second.empty() ? 0 : std::stoi(found->second);
}
} // namespace
void assignShrine(WorldObject &object, const Table &rows, int levelId, uint64_t &random) {
    if (object.interaction != Interaction::Shrine) return;
    if (rows.size() < 2) throw std::runtime_error("MPQ Shrines.txt has no usable types");
    int shrineClass = 0;
    if (object.parameters[0]) {
        shrineClass = object.parameters[0] == 1 ? 2 : object.parameters[0] == 2 ? 3 : 4;
        if (shrineClass == 4 && limitedRandom(random, 10) == 0) shrineClass = 1;
    }
    std::vector<const std::map<std::string, std::string> *> candidates;
    for (const auto &row : rows)
        if (number(row, "Code") > 0 && (!shrineClass || number(row, "effectclass") == shrineClass))
            candidates.push_back(&row);
    if (candidates.empty()) throw std::runtime_error("MPQ Shrines.txt lacks the selected effect class");
    const std::map<std::string, std::string> *chosen = nullptr;
    for (int attempt = 0; attempt < 8; ++attempt) {
        chosen = candidates[size_t(limitedRandom(random, uint32_t(candidates.size())))];
        if (levelId >= number(*chosen, "LevelMin")) break;
    }
    int code = number(*chosen, "Code");
    // Native shrine initialization replaces the retired exchange and Enirhs effects.
    if (code == 4) code = 2;
    else if (code == 5) code = 3;
    else if (code == 16) code = 18;
    auto resolved = std::find_if(rows.begin(), rows.end(), [code](const auto &row) {
        return number(row, "Code") == code;
    });
    if (resolved == rows.end()) throw std::runtime_error("MPQ shrine replacement row is missing");
    object.shrineCode = code;
    object.shrineName = resolved->at("Shrine name");
    object.shrineEffect = resolved->at("Effect");
    object.shrineDuration = float(number(*resolved, "Duration in frames")) / 25.f;
    object.shrineReset = number(*resolved, "reset time in minutes") > 0
        ? float(number(*resolved, "reset time in minutes") * 1200 + 1) / 25.f : 0.f;
}
} // namespace d2x

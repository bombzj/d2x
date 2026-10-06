#include "curse_resolve.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
CurseSpec evaluateCurse(const CurseSpec &spec, int rank) {
    if (rank < 1 || rank > 255) throw std::runtime_error("Unsupported original curse rank");
    auto result = spec;
    result.radius += (rank - 1) * result.radiusPerLevel;
    result.frames += (rank - 1) * result.framesPerLevel;
    result.modifiers.combat.ironMaidenPercent = result.reflectPercent + (rank - 1) * result.reflectPerLevel;
    result.modifiers.combat.lifeTapPercent = result.lifeTapPercent + (rank - 1) * result.lifeTapPerLevel;
    if (result.resistMaximum > 0) {
        // Keep the intermediate integer ratio. Current MPQ parameters yield
        // Blizzard's published Lower Resist values (rank 1 = 31, rank 20 = 62).
        // D2MOO's reconstructed 11033 uses different grouping; see the baseline.
        const int ratio = 110 * rank / (rank + 6);
        const int amount = -std::min(result.resistMaximum, result.resistMinimum +
            (result.resistMaximum - result.resistMinimum) * ratio / 100);
        auto &modifiers = result.modifiers;
        modifiers.fireResist = modifiers.coldResist = modifiers.lightningResist = modifiers.poisonResist = amount;
    }
    return result;
}
} // namespace d2x

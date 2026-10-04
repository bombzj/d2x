#include "curse_resolve.hpp"
#include "gameplay/effects/state.hpp"
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
CharacterModifiers evaluateCurseModifiers(CharacterModifiers modifiers, const CharacterAttributes &target,
    std::span<const ActiveCombatEffect> effects, EffectFrame frame, bool diminishAgainstImmunity) {
    if (!diminishAgainstImmunity) return modifiers;
    auto base = target;
    for (const auto &effect : effects) {
        if (!effect.activeAt(frame)) continue;
        const auto &active = effect.spec.modifiers;
        base.combat.physicalResist -= active.combat.physicalResist;
        base.combat.magicResist -= active.combat.magicResist;
        base.fireResist -= active.fireResist; base.coldResist -= active.coldResist;
        base.lightningResist -= active.lightningResist; base.poisonResist -= active.poisonResist;
    }
    // SkillNec::sub_6FD0B3D0: inspect the original base stat, not the value
    // reduced by an existing curse or aura. Each immunity is independent.
    auto reduce = [](int &amount, int original) { if (amount < 0 && original >= 100) amount /= 5; };
    reduce(modifiers.combat.physicalResist, base.combat.physicalResist);
    reduce(modifiers.combat.magicResist, base.combat.magicResist);
    reduce(modifiers.fireResist, base.fireResist); reduce(modifiers.coldResist, base.coldResist);
    reduce(modifiers.lightningResist, base.lightningResist); reduce(modifiers.poisonResist, base.poisonResist);
    return modifiers;
}
} // namespace d2x

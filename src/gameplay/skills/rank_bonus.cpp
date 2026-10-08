#include "gameplay/skills/rank_bonus.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace d2x {
int skillRankBonus(std::pair<int, int> curve, int rank) {
    return rank > 0 ? curve.first + (rank - 1) * curve.second : 0;
}
int skillDiminishingBonus(std::pair<int, int> bounds, int rank) {
    if (rank <= 0) return 0;
    const auto [minimum, maximum] = bounds;
    return int(std::min<int64_t>(maximum, int64_t(minimum) +
        (int64_t(maximum) - minimum) * 110 * rank / (rank + 6) / 100));
}
ShieldAbsorption absorbSkillShield(float damage, float manaValue, int percent, int manaFactor) {
    if (manaFactor <= 0) throw std::invalid_argument("Invalid skill shield mana factor");
    int64_t mana = int64_t(manaValue * 256.f);
    const int64_t fixed = std::max<int64_t>(0, int64_t(damage * 256.f));
    const int64_t absorbed = std::min(fixed * percent / 100, mana * 16 / manaFactor);
    mana = std::max<int64_t>(0, mana - absorbed * manaFactor / 16);
    return {float(fixed - absorbed) / 256.f, float(mana) / 256.f};
}
} // namespace d2x

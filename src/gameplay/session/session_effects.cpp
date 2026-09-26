#include "session.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void GameSession::applyCombatEffect(ActiveCombatEffect effect) {
    if (effect.sourceId < 0 || effect.expiresAt <= state().time || state().player.dead)
        throw std::invalid_argument("Invalid combat effect source or duration");
    auto &effects = simulation_.state_.player.combatEffects;
    std::erase_if(effects, [&](const auto &existing) {
        return existing.source == effect.source && existing.owner == effect.owner &&
             (existing.sourceId == effect.sourceId || (effect.group > 0 && existing.group == effect.group));
    });
    effects.push_back(std::move(effect));
    refreshCharacter(true);
}
void GameSession::expireCombatEffects() {
    auto &effects = simulation_.state_.player.combatEffects;
    const auto before = effects.size();
    std::erase_if(effects, [&](const auto &effect) {
        return state().player.dead || effect.expiresAt <= state().time;
    });
    if (effects.size() != before) refreshCharacter();
}
} // namespace d2x

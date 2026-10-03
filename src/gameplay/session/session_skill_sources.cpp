#include "gameplay/skills/spec.hpp"
#include "gameplay/session/session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/skills/resolve.hpp"
#include <stdexcept>

namespace d2x {
void GameSessionImpl::configureSkillSources() {
    const EntityId actor = state().player.id;
    skillSources_.bind(actor, [this, actor] {
        // Restore replaces the state value; fetch it anew rather than retaining
        // a PlayerState reference or caching modifiers at cast start.
        const auto &player = state().player;
        if (player.id != actor) throw std::runtime_error("Bound skill source actor changed");
        return SkillSourceValues{player.skillRanks, fireMasteryPercent(), lightningMasteryPercent(),
                                 characterStats().combat.coldSkillDamagePercent};
    });

    simulation_->fireMastery_ = [this](EntityId source) {
        if (source == state().player.id) return fireMasteryPercent();
        if (simulation_->findEnemy(source)) return 0; // Existing native monster sources have no player mastery.
        throw std::runtime_error("Fire mastery source unavailable for this actor");
    };

    simulation_->resolveUnitSkill_ = [this](EntityId actor, int id, int rank) {
        const auto *entry = content_.skills.find(id);
        if (!entry || !entry->spell) throw std::runtime_error("Missing originating missile skill");
        return skillSources_.resolve(*entry->spell, actor, rank);
    };

    // Existing native monster missiles have explicit ranks and no player
    // synergies or masteries. Keep their original path and rules.
    simulation_->monsterSpecialMissile_ = [this](int id, int rank) -> std::optional<NativeSkillCast> {
        auto found = content_.monsterSpecialMissiles.find(id);
        if (found == content_.monsterSpecialMissiles.end()) return std::nullopt;
        return NativeSkillCast{resolveSkill(found->second.spec, {rank, {}}),
            MonsterDamageType(found->second.element), found->second.killOnHit};
    };
}
} // namespace d2x

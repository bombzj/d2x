#include "session.hpp"
#include "content/monsters/monster_enchantment.hpp"

namespace d2x {
std::optional<MonsterCombatProfile> GameSession::resolvedMonsterCombat(
    const MonsterIdentity &identity, RegionId region) const {
    if (!identity.enchantment && identity.rank != MonsterRank::Normal && identity.rank != MonsterRank::Minion &&
        identity.monster != "bloodraven" && identity.monster != "andariel")
        return std::nullopt;
    const auto key = std::pair{identity.monster, region};
    if (auto cached = monsterCombatCache_.find(key); cached != monsterCombatCache_.end())
        return cached->second && identity.enchantment
            ? enchantedMonsterCombat(*cached->second, *identity.enchantment) : cached->second;
    const auto *monster = monsterContent_.find(identity.monster);
    const auto area = worldContent_.levels().find(int(region));
    if (!monster || !monster->hostile() || (monster->boss && monster->id != "griswold" &&
        monster->id != "bloodraven" && monster->id != "andariel") || area == worldContent_.levels().end() ||
        !area->second.population.supported)
        return std::nullopt;
    const int difficulty = state().population.difficulty;
    if (difficulty < 0 || difficulty > 2) return std::nullopt;
    auto profile = loadMonsterCombatProfile(content_.tables.at("monstats"), monster->sourceRow,
                                            content_.tables.at("monlvl"), difficulty,
                                            area->second.population.level[difficulty]);
    monsterCombatCache_.emplace(key, profile);
    return profile && identity.enchantment ? enchantedMonsterCombat(*profile, *identity.enchantment) : profile;
}
} // namespace d2x

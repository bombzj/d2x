#include "character_action_stats.hpp"
#include "gameplay/session/session.hpp"
#include <cstdint>

namespace d2x {
namespace {
std::string damageText(int64_t minimum, int64_t maximum) {
    return std::to_string(minimum) + "-" + std::to_string(maximum);
}
std::string weaponDamage(const GameSession &session, bool thrown, bool leftHand) {
    const auto &equipment = session.equipmentStats();
    const auto &combat = session.characterStats().combat;
    std::string value;
    for (int index = 0; index < equipment.weaponCount; ++index) {
        const auto &weapon = equipment.weapons[index];
        if ((thrown && !weapon.throwable) || (leftHand && !weapon.leftHand)) continue;
        // Ordinary melee alternates dual weapons; ordinary ranged fire uses the first weapon.
        if (!thrown && !leftHand && index > 0 && equipment.weapons[0].ranged) break;
        const auto elements = attackElementRanges(combat, weapon.item);
        const int64_t elementalMinimum = int64_t(elements.fire.minimum) + elements.lightning.minimum +
                                         elements.cold.minimum + elements.magic.minimum;
        const int64_t elementalMaximum = int64_t(elements.fire.maximum) + elements.lightning.maximum +
                                         elements.cold.maximum + elements.magic.maximum;
        const int minimum = thrown ? weapon.throwMinimum : weapon.minimum;
        const int maximum = thrown ? weapon.throwMaximum : weapon.maximum;
        if (!value.empty()) value += "/";
        value += damageText(int64_t(minimum) / 256 + elementalMinimum,
                            int64_t(maximum) / 256 + elementalMaximum);
        if (thrown || leftHand) break;
    }
    return value;
}
} // namespace

CharacterActionStats characterActionStats(const GameSession &session, std::optional<int> skill) {
    const auto rating = std::to_string(session.characterStats().attackRating);
    const auto normal = [&]() { return CharacterActionStats{weaponDamage(session, false, false), rating}; };
    if (!skill) return normal();
    const auto *entry = session.content().skills.find(*skill);
    if (!entry || !session.skillAvailable(*skill) || entry->passive) return {};
    if (entry->classCode.empty()) {
        if (entry->sourceName == "Unsummon") return {};
        const bool thrown = entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw";
        const bool leftHand = entry->sourceName == "Left Hand Throw" ||
                              entry->sourceName == "Left Hand Swing";
        auto damage = weaponDamage(session, thrown, leftHand);
        return {damage, damage.empty() ? "" : rating};
    }
    if (entry->originalEffect) {
        const int rank = session.effectiveSkillRank(*skill);
        if (rank < 1) return {};
        const auto cast = resolveOriginalSkill(*entry->originalEffect, rank,
                                               session.state().player.skillRanks,
                                               session.fireMasteryPercent(),
                                               session.lightningMasteryPercent());
        if (cast.effect == Skill::Teleport || cast.effect == Skill::StaticField ||
            cast.effect == Skill::FrozenArmor) return {};
        return {damageText(int64_t(cast.minimumDamage), int64_t(cast.maximumDamage)), ""};
    }
    if (auto effect = implementedSkillEffect(*entry)) {
        const auto &definition = skillDefinition(*effect);
        if (*effect == Skill::Whirlwind)
            return {std::to_string(int(definition.damage)) + "/s", ""};
        if (*effect == Skill::Leap || *effect == Skill::WarCry)
            return {damageText(int(definition.damage), int(definition.damage)), ""};
        return {};
    }
    // Class skills without their own effect currently execute an ordinary attack.
    return normal();
}
} // namespace d2x

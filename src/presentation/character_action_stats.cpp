#include "character_action_stats.hpp"
#include "gameplay/session/session.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace d2x {
namespace {
std::string damageText(int64_t minimum, int64_t maximum) {
    return std::to_string(minimum) + "-" + std::to_string(maximum);
}
CharacterActionStats weaponStats(const GameSession &session, bool thrown, bool leftHand,
                                 const SkillCastSpec *skill = nullptr) {
    const auto &equipment = session.equipmentStats();
    const auto &combat = session.characterStats().combat;
    for (int index = 0; index < equipment.weaponCount; ++index) {
        const auto &weapon = equipment.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if ((thrown && !weapon.throwable) || (!thrown && weapon.potion)) return {};
        if (skill && std::find(weapon.types.begin(), weapon.types.end(), skill->weapon->requiredType) == weapon.types.end())
            return {};
        if (thrown && weapon.potion && weapon.projectile) {
            const auto &spec = *weapon.projectile;
            if (spec.impact && spec.impact->cloudBurst) {
                const auto &cloud = spec.impact->cloudBurst->cloud;
                return {damageText(int64_t(cloud.minimum) * cloud.poisonFrames / 256,
                                   int64_t(cloud.maximum) * cloud.poisonFrames / 256) +
                        " / " + std::to_string(cloud.poisonFrames / 25) + "s", ""};
            }
            int64_t minimum = 0, maximum = 0;
            for (const auto &range : spec.damage) { minimum += range.minimum; maximum += range.maximum; }
            return {damageText(minimum / 256, maximum / 256), ""};
        }
        const auto elements = attackElementRanges(combat, weapon.item);
        const int64_t elementalMinimum = int64_t(elements.fire.minimum) + elements.lightning.minimum +
                                         elements.cold.minimum + elements.magic.minimum;
        const int64_t elementalMaximum = int64_t(elements.fire.maximum) + elements.lightning.maximum +
                                         elements.cold.maximum + elements.magic.maximum;
        const int minimum = thrown ? weapon.throwMinimum : weapon.minimum;
        const int maximum = thrown ? weapon.throwMaximum : weapon.maximum;
        int64_t poisonMinimum = 0, poisonMaximum = 0;
        if (skill && skill->poisonDuration > 0) {
            WeaponModifiers own;
            if (auto found = combat.weapons.find(weapon.item); found != combat.weapons.end()) own = found->second;
            const int64_t frames = (int64_t(skill->poisonDuration * 25.f + .001f) + combat.poisonFrames + own.poisonFrames) /
                std::max(1, combat.poisonSources + own.poisonSources);
            poisonMinimum = (int64_t(skill->minimumDamage * 256.f) + combat.poisonMinimum + own.poisonMinimum) * frames / 256;
            poisonMaximum = (int64_t(skill->maximumDamage * 256.f) + combat.poisonMaximum + own.poisonMaximum) * frames / 256;
        }
        const int rating = int(std::clamp<int64_t>(int64_t(weapon.baseAttackRating) *
            std::max(0, 100 + weapon.attackRatingPercent + (skill ? skill->weapon->attackRating : 0)) / 100,
            0, std::numeric_limits<int>::max()));
        const int64_t skillMinimum = skill && skill->poisonDuration == 0 ? int64_t(skill->minimumDamage) : 0;
        const int64_t skillMaximum = skill && skill->poisonDuration == 0 ? int64_t(skill->maximumDamage) : 0;
        return {damageText(int64_t(minimum) / 256 + elementalMinimum + poisonMinimum + skillMinimum,
                           int64_t(maximum) / 256 + elementalMaximum + poisonMaximum + skillMaximum), std::to_string(rating)};
    }
    return {};
}
} // namespace

CharacterActionStats characterActionStats(const GameSession &session, std::optional<int> skill) {
    if (!skill) return weaponStats(session, false, false);
    const auto *entry = session.content().skills.find(*skill);
    if (!entry || !session.skillAvailable(*skill) || entry->passive) return {};
    if (entry->basicAction != BasicSkillAction::None) {
        const bool thrown = entry->basicAction == BasicSkillAction::Throw || entry->basicAction == BasicSkillAction::LeftHandThrow;
        const bool leftHand = entry->basicAction == BasicSkillAction::LeftHandThrow ||
                              entry->basicAction == BasicSkillAction::LeftHandSwing;
        return weaponStats(session, thrown, leftHand);
    }
    if (entry->spell) {
        const int rank = session.effectiveSkillRank(*skill);
        if (rank < 1) return {};
        const auto cast = resolveSkill(*entry->spell, rank,
                                               session.state().player.skillRanks,
                                               session.fireMasteryPercent(),
                                               session.lightningMasteryPercent(),
                                               session.characterStats().combat.coldSkillDamagePercent);
        if (cast.weapon) return weaponStats(session, cast.weapon->thrown, false, &cast);
        if (cast.effect == SkillBehavior::Teleport || cast.effect == SkillBehavior::StaticField ||
            cast.effect == SkillBehavior::FrozenArmor) return {};
        if (cast.effect == SkillBehavior::Inferno)
            return {damageText(int64_t(cast.minimumDamage * 25), int64_t(cast.maximumDamage * 25)) + "/s", ""};
        return {damageText(int64_t(cast.minimumDamage), int64_t(cast.maximumDamage)), ""};
    }
    return {};
}
} // namespace d2x

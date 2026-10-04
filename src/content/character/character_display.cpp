#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/resolve.hpp"
#include "character_display.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "content/skills/skill_data.hpp"
#include "gameplay/skills/aura.hpp"
#include "gameplay/skills/passive.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <tuple>

namespace d2x {
namespace {
template<class... Args> std::string displayNumber(const char *format, Args... values) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), format, values...);
    return buffer;
}
std::string damageText(int64_t minimum, int64_t maximum) {
    return std::to_string(minimum) + "-" + std::to_string(maximum);
}
CharacterActionDisplay weaponStats(const CharacterDisplayContext &context, bool thrown, bool leftHand,
                                 const SkillCastSpec *skill = nullptr) {
    const auto &equipment = context.equipment;
    const auto &combat = context.attributes.combat;
    for (int index = 0; index < equipment.weaponCount; ++index) {
        const auto &weapon = equipment.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if ((thrown && !weapon.throwable) || (!thrown && weapon.potion)) return {};
        if (skill && !skill->weapon->smite && std::find(weapon.types.begin(), weapon.types.end(), skill->weapon->requiredType) == weapon.types.end())
            return {};
        if (skill && skill->weapon->smite) {
            if (!equipment.shield) return {};
            const int percent = std::max(0, 100 + context.attributes.strength + combat.damagePercent + skill->weapon->damagePercent);
            return {damageText(int64_t(equipment.smiteMinimum + combat.smiteMinimum + combat.normalDamage) * percent / 100,
                int64_t(equipment.smiteMaximum + combat.smiteMaximum + combat.normalDamage) * percent / 100), ""};
        }
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
        int64_t elementalMinimum = int64_t(elements.fire.minimum) + elements.lightning.minimum +
                                         elements.cold.minimum + elements.magic.minimum;
        int64_t elementalMaximum = int64_t(elements.fire.maximum) + elements.lightning.maximum +
                                         elements.cold.maximum + elements.magic.maximum;
        if (skill && skill->effect == SkillBehavior::Vengeance)
            for (int percent : skill->weapon->elementPercent) {
                elementalMinimum += int64_t(weapon.meleeBaseMinimum) * percent / 100 / 256;
                elementalMaximum += int64_t(weapon.meleeBaseMaximum) * percent / 100 / 256;
            }
        const int skillDamage = skill ? skill->weapon->damagePercent : 0;
        int64_t minimum = thrown ? weapon.throwMinimum : weapon.minimum +
            int(int64_t(weapon.meleeBaseMinimum) * skillDamage / 100);
        int64_t maximum = thrown ? weapon.throwMaximum : weapon.maximum +
            int(int64_t(weapon.meleeBaseMaximum) * skillDamage / 100);
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
        int64_t skillMinimum = skill && skill->poisonDuration == 0 ? int64_t(skill->minimumDamage) : 0;
        int64_t skillMaximum = skill && skill->poisonDuration == 0 ? int64_t(skill->maximumDamage) : 0;
        if (skill && skill->weapon->bow) {
            const auto &bow = *skill->weapon->bow;
            const int percent = std::max(0, 100 + weapon.projectileDamagePercent + skillDamage);
            minimum = (int64_t(weapon.projectileMinimum) * bow.sourceDamage / 128 +
                (bow.physicalSkillDamage ? int64_t(skill->minimumDamage * 256.f) : 0)) * percent / 100;
            maximum = (int64_t(weapon.projectileMaximum) * bow.sourceDamage / 128 +
                (bow.physicalSkillDamage ? int64_t(skill->maximumDamage * 256.f) : 0)) * percent / 100;
            elementalMinimum = elementalMinimum * bow.sourceDamage / 128;
            elementalMaximum = elementalMaximum * bow.sourceDamage / 128;
            if (bow.physicalSkillDamage) skillMinimum = skillMaximum = 0;
        }
        return {damageText(int64_t(minimum) / 256 + elementalMinimum + poisonMinimum + skillMinimum,
                           int64_t(maximum) / 256 + elementalMaximum + poisonMaximum + skillMaximum), skill && skill->weapon->bow && skill->weapon->bow->guided ? "" : std::to_string(rating)};
    }
    return {};
}
} // namespace

CharacterActionDisplay describeCharacterAction(const CharacterDisplayContext &context,
    const SkillRecord *entry, int rank, bool available) {
    if (!entry) return weaponStats(context, false, false);
    if (!available || entry->passive) return {};
    if (entry->auraImplemented) return weaponStats(context, false, false);
    if (entry->basicAction != BasicSkillAction::None) {
        const bool thrown = entry->basicAction == BasicSkillAction::Throw || entry->basicAction == BasicSkillAction::LeftHandThrow;
        const bool leftHand = entry->basicAction == BasicSkillAction::LeftHandThrow ||
                              entry->basicAction == BasicSkillAction::LeftHandSwing;
        return weaponStats(context, thrown, leftHand);
    }
    if (entry->spell) {
        if (rank < 1) return {};
        const auto cast = resolveSkill(*entry->spell, {rank, context.learned, context.fireMastery,
            context.lightningMastery, context.attributes.combat.coldSkillDamagePercent});
        if (cast.curse) return {};
        if (cast.weapon) return weaponStats(context, cast.weapon->thrown, false, &cast);
        if (cast.effect == SkillBehavior::Teleport || cast.effect == SkillBehavior::StaticField ||
            cast.effect == SkillBehavior::FrozenArmor) return {};
        if (cast.effect == SkillBehavior::Inferno)
            return {damageText(int64_t(cast.minimumDamage * 25), int64_t(cast.maximumDamage * 25)) + "/s", ""};
        return {damageText(int64_t(cast.minimumDamage), int64_t(cast.maximumDamage)), ""};
    }
    return {};
}

std::vector<std::string> describeAuraSkill(const SkillRecord &skill, const AuraDefinition &definition, int baseRank) {
    const auto *aura = &definition;
    std::vector<std::string> lines;
    auto number = [](float value) { return std::string(displayNumber("%.1f", value)); };
    const auto &stats = aura->modifiers;
    const auto &combat = stats.combat;
    lines.push_back("Radius: " + number(aura->radius * 2.f / 3.f) + " yards");
    if (combat.damagePercent)
        lines.push_back("Damage: +" + std::to_string(combat.damagePercent + aura->ownerDamageBonus) + "%" +
            (aura->ownerDamageBonus ? " / Party: +" + std::to_string(combat.damagePercent) + "%" : ""));
    if (combat.attackRatingPercent) lines.push_back("Attack rating: +" + std::to_string(combat.attackRatingPercent) + "%");
    if (combat.defensePercent) lines.push_back("Defense: " + std::string(combat.defensePercent > 0 ? "+" : "") + std::to_string(combat.defensePercent) + "%");
    if (combat.attackRate > 0) lines.push_back("Attack speed: +" + std::to_string(combat.attackRate) + "%");
    if (stats.velocityPercent < 0) lines.push_back("Slows enemies: " + std::to_string(-stats.velocityPercent) + "%");
    else if (stats.velocityPercent > 0) lines.push_back("Movement speed: +" + std::to_string(stats.velocityPercent) + "%");
    for (const auto &[label, resist, maximum] : std::vector<std::tuple<const char *, int, int>>{
        {"Fire", stats.fireResist, combat.fireMaxResist}, {"Cold", stats.coldResist, combat.coldMaxResist},
        {"Lightning", stats.lightningResist, combat.lightningMaxResist}}) {
        if (!resist && !maximum) continue;
        std::string line = std::string(label) + " resist: " + (resist > 0 ? "+" : "") + std::to_string(resist) + "%";
        if (maximum) line += " / Maximum: +" + std::to_string(maximum) + "%";
        lines.push_back(std::move(line));
    }
    if (aura->element >= 0) {
        const char *element = aura->element == 2 ? "Fire" : aura->element == 3 ? "Lightning" : aura->element == 4 ? "Cold" : "Magic";
        lines.push_back(std::string(element) + " damage: " + number(aura->minimumDamage) + "-" + number(aura->maximumDamage) +
            " / " + number(float(aura->periodFrames) / 25.f) + " seconds");
        const auto &own = aura->ownerModifiers.combat;
        const int minimum = own.fireMinimum + own.lightningMinimum + own.coldMinimum;
        const int maximum = own.fireMaximum + own.lightningMaximum + own.coldMaximum;
        if (maximum) lines.push_back("Attack damage: +" + std::to_string(minimum) + "-" + std::to_string(maximum));
    }
    if (aura->lifePerPulse > 0) lines.push_back("Heals: " + number(aura->lifePerPulse) + " / " + number(float(aura->periodFrames) / 25.f) + " seconds");
    if (aura->harmfulDurationPercent < 100) lines.push_back("Poison / curse duration reduction: " + std::to_string(100 - aura->harmfulDurationPercent) + "%");
    if (combat.manaRecovery) lines.push_back("Mana recovery: +" + std::to_string(combat.manaRecovery) + "%");
    if (stats.staminaPercent) lines.push_back("Maximum stamina: +" + std::to_string(stats.staminaPercent) + "%");
    if (stats.staminaRecoveryBonus) lines.push_back("Stamina recovery: +" + std::to_string(stats.staminaRecoveryBonus) + "%");
    if (combat.thornsPercent) lines.push_back("Damage returned: " + std::to_string(combat.thornsPercent) + "%");
    if (combat.concentrationChance) lines.push_back("Uninterruptible attack chance: " + std::to_string(combat.concentrationChance) + "%");
    if (aura->redemptionChance) {
        lines.push_back("Corpse redemption chance: " + std::to_string(aura->redemptionChance) + "%");
        lines.push_back("Life / mana per corpse: " + number(aura->redemptionLife) + " / " + number(aura->redemptionMana));
    }
    if (aura->manaPerPulse > 0) lines.push_back("Mana per pulse: " + number(aura->manaPerPulse));
    CharacterModifiers passive;
    applySkillPassive(passive, skill.passiveContribution, baseRank, false);
    if (skill.passiveContribution.attackRatingPerBaseRank)
        lines.push_back("Passive attack rating: +" + std::to_string(passive.combat.attackRatingPercent) + "%");
    if (skill.passiveContribution.maxResistElement >= 0)
        lines.push_back("Passive maximum resist: +" + std::to_string(passive.combat.fireMaxResist +
            passive.combat.coldMaxResist + passive.combat.lightningMaxResist) + "%");
    return lines;
}

std::vector<std::string> describeSkillPicker(const SkillRecord &skill, const SkillCastSpec *resolved,
    const AuraDefinition *aura, int baseRank, int aiCurseDivisor) {
    const auto *entry = &skill;
    std::string detail;
    std::vector<std::string> detailLines;
    if (resolved) {
        const auto &value = *resolved;
        detail = "Mana " + std::string(displayNumber("%.1f", value.manaCost));
        if (value.summon) {
            const auto &pet = *value.summon;
            const auto &stats = pet.stats.attributes;
            detail += " / Summons " + std::to_string(pet.limit);
            if (pet.necro && pet.necro->kind == NecroSummonKind::Revive)
                detail += " / Life +" + std::to_string(pet.necro->lifePercent) + "% / Physical damage +" +
                    std::to_string(pet.necro->damagePercent) + "% / " + std::to_string(pet.necro->lifetimeFrames / 25) + " seconds";
            else detail += " / Life " + std::to_string(stats.maxLife) + " / Attack rating " +
                std::to_string(stats.attackRating) + " / Defense " + std::to_string(stats.defense);
            if (pet.necro && pet.necro->slowPercent) detail += " / Slows " + std::to_string(pet.necro->slowPercent) + "%";
        }
        else if (value.effect == SkillBehavior::Teleport) detail += " / Teleport to clear ground";
        else if (value.effect == SkillBehavior::Teeth)
            detail += " / Teeth " + std::to_string(value.missileCount) + " / Magic " +
                std::string(displayNumber("%.1f-%.1f", value.minimumDamage, value.maximumDamage));
        else if (value.bone && value.bone->barrier)
            detail += " / Barrier life +" + std::to_string(value.bone->lifePercent) + "% / " +
                std::to_string(value.bone->barrierFrames / 25) + " seconds";
        else if (value.effect == SkillBehavior::CorpseExplosion)
            detail += " / Corpse life " + std::to_string(value.bone->minimumPercent) + "-" +
                std::to_string(value.bone->maximumPercent) + "% / Half physical, half fire / Radius " +
                std::string(displayNumber("%.1f yards", float(value.bone->radius) / 3.f));
        else if (value.curse) {
            const int divisor = value.curse->ai == CurseAi::None ? 1 : std::max(1, aiCurseDivisor);
            detail += " / Radius " + std::to_string(value.curse->radius) +
                " / " + std::string(displayNumber("%.1fs", float(value.curse->frames / divisor) / 25.f));
            if (value.curse->modifiers.combat.ironMaidenPercent > 0)
                detail += " / Melee return " + std::to_string(value.curse->modifiers.combat.ironMaidenPercent) + "%";
            if (value.curse->modifiers.combat.lifeTapPercent > 0)
                detail += " / Physical healing " + std::to_string(value.curse->modifiers.combat.lifeTapPercent) + "%";
        }
        else if (value.effect == SkillBehavior::HolyBolt)
            detail += " / Undead magic " + std::string(displayNumber("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                " / Ally healing " + std::string(displayNumber("%.1f-%.1f", value.healingMinimum, value.healingMaximum));
        else if (value.effect == SkillBehavior::Inferno)
            detail = "Mana/sec " + std::string(displayNumber("%.1f", value.manaCost * 12.5f)) +
                " / Damage/sec " + std::string(displayNumber("%.1f-%.1f", value.minimumDamage * 25, value.maximumDamage * 25));
        else if (value.effect == SkillBehavior::BoneArmor)
            detail += " / Absorbs " + std::to_string(value.appliedEffect->physicalShieldMaximum / 256) + " physical damage";
        else if (value.appliedEffect) {
            detail += " / Defense +" + std::to_string(value.appliedEffect->modifiers.combat.defensePercent +
                value.appliedEffect->modifiers.combat.shieldDefensePercent) + "% / " +
                std::to_string(value.appliedEffect->duration.value() / 25) + " seconds";
            if (value.effect == SkillBehavior::HolyShield)
                detail += " / Block +" + std::to_string(value.appliedEffect->modifiers.combat.blockBonus) +
                    " / Smite " + std::to_string(value.appliedEffect->modifiers.combat.smiteMinimum) +
                    "-" + std::to_string(value.appliedEffect->modifiers.combat.smiteMaximum);
            if (value.effect == SkillBehavior::ShiverArmor || value.effect == SkillBehavior::ChillingArmor)
                detail += " / Retaliate cold " + std::string(displayNumber("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                    " / Chill " + std::string(displayNumber("%.1fs", value.coldDuration));
        }
        else if (value.effect == SkillBehavior::StaticField)
            detail += " / " + std::to_string(int(value.staticPercent)) + "% current life, range " +
                std::to_string(int(value.staticRadius));
        else if (value.blizzard)
            detail += " / Cold damage per shard " + std::string(displayNumber("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                " / Duration " + std::string(displayNumber("%.1fs", value.missileLifetime)) +
                " / Delay " + std::string(displayNumber("%.1fs", float(value.delayFrames) / 25.f));
        else if (value.frozenOrb)
            detail += " / Cold damage per bolt " + std::string(displayNumber("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                " / Chill " + std::string(displayNumber("%.1fs", value.coldDuration)) +
                " / Delay " + std::string(displayNumber("%.1fs", float(value.delayFrames) / 25.f));
        else if (value.freezingArea)
            detail += " / Cold damage " + std::string(displayNumber("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                " / Freeze " + std::string(displayNumber("%.2fs", float(value.freezingArea->freezeFrames) / 25.f));
        else if (value.poisonDuration > 0)
            detail += " / Poison " + std::string(displayNumber("%.1f-%.1f over %.1fs",
                value.minimumDamage * value.poisonDuration * 25.f,
                value.maximumDamage * value.poisonDuration * 25.f, value.poisonDuration));
        else if (value.effect == SkillBehavior::Zeal)
            detail += " / Attacks " + std::to_string(value.weapon->attacks) +
                " / Physical +" + std::to_string(value.weapon->damagePercent) + "%";
        else if (value.effect == SkillBehavior::Vengeance)
            detail += " / Fire " + std::to_string(value.weapon->elementPercent[0]) +
                "% / Cold " + std::to_string(value.weapon->elementPercent[1]) +
                "% / Lightning " + std::to_string(value.weapon->elementPercent[2]) + "%";
        else if (value.effect == SkillBehavior::Sacrifice)
            detail += " / Physical +" + std::to_string(value.weapon->damagePercent) +
                "% / Attack +" + std::to_string(value.weapon->attackRating) +
                "% / Self damage " + std::to_string(value.weapon->selfDamagePercent) + "%";
        else if (value.effect == SkillBehavior::Smite)
            detail += " / Shield damage +" + std::to_string(value.weapon->damagePercent) +
                "% / Stun " + std::string(displayNumber("%.2fs", float(value.weapon->stunFrames) / 25.f));
        else if (value.effect == SkillBehavior::Conversion)
            detail += " / Convert " + std::to_string(value.weapon->conversionChance) +
                "% / " + std::to_string(value.weapon->conversionFrames / 25) + " seconds";
        else if (value.effect == SkillBehavior::Charge)
            detail += " / Physical +" + std::to_string(value.weapon->damagePercent) +
                "% / Attack +" + std::to_string(value.weapon->attackRating) + "%";
        else if (value.weapon && value.missileImpact && value.missileImpact->areaMissile)
            detail += " / Fire " + std::string(displayNumber("%.1f-%.1f", value.minimumDamage, value.maximumDamage)) +
                " + weapon fire damage";
        else detail += " / Damage " + std::string(displayNumber("%.1f", value.minimumDamage)) +
            "-" + std::string(displayNumber("%.1f", value.maximumDamage));
    } else if (entry->auraImplemented) {
        if (aura) detailLines = describeAuraSkill(skill, *aura, baseRank);
    } else detail = !entry ? "Normal weapon attack"
        : entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw"
            ? "Throw equipped weapon; consumes one from the stack"
        : entry->basicAction == BasicSkillAction::Attack || entry->basicAction == BasicSkillAction::LeftHandSwing
            ? "Uses the current basic melee damage"
        : entry->sourceName == "Unsummon"
            ? "No summoned ally is available"
            : "Effect not implemented";
    if (detailLines.empty()) detailLines.push_back(detail);
    return detailLines;
}
} // namespace d2x

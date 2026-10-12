#include "gameplay/combat/poison.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/resolve.hpp"
#include "character_display.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "content/skills/skill_data.hpp"
#include "gameplay/skills/amazon_magic_spec.hpp"
#include "content/skills/description_values.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace d2x {
namespace {
std::string damageText(int64_t minimum, int64_t maximum) {
    return std::to_string(minimum) + "-" + std::to_string(maximum);
}
CharacterActionDisplay weaponStats(const CharacterDisplayContext &context, bool thrown, bool leftHand,
                                 const SkillCastSpec *skill = nullptr) {
    if (!context.weaponValuesKnown) return {"?", "?"};
    const auto &equipment = context.equipment;
    const auto &combat = context.attributes.combat;
    for (int index = 0; index < equipment.weaponCount; ++index) {
        const auto &weapon = equipment.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if ((thrown && !weapon.throwable) || (!thrown && weapon.potion)) return {};
        if (skill && !skill->weapon->smite && !skill->weapon->requiredType.empty() && std::find(weapon.types.begin(), weapon.types.end(), skill->weapon->requiredType) == weapon.types.end())
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
        WeaponModifiers own;
        if (auto found = combat.weapons.find(weapon.item); found != combat.weapons.end()) own = found->second;
        const auto itemMinimum=poisonMasteryRate(int64_t(combat.poisonMinimum)+own.poisonMinimum,combat.poisonSkillDamagePercent);
        const auto itemMaximum=poisonMasteryRate(int64_t(combat.poisonMaximum)+own.poisonMaximum,combat.poisonSkillDamagePercent);
        const auto itemFrames=weaponPoisonLength(int64_t(combat.poisonFrames)+own.poisonFrames,combat.poisonSources+own.poisonSources);
        int64_t poisonMinimum=itemMinimum*itemFrames/256,poisonMaximum=itemMaximum*itemFrames/256;
        if(skill && skill->poisonDuration>0) {
            const auto skillFrames=int64_t(skill->poisonDuration*25.f+.001f);
            if(skill->weapon->spear) {
                poisonMinimum=int64_t(skill->minimumDamage*256.f)*skillFrames/256;
                poisonMaximum=int64_t(skill->maximumDamage*256.f)*skillFrames/256;
            } else {
                const auto frames=weaponPoisonLength(int64_t(combat.poisonFrames)+own.poisonFrames,combat.poisonSources+own.poisonSources,skillFrames);
                poisonMinimum=(int64_t(skill->minimumDamage*256.f)+itemMinimum)*frames/256;
                poisonMaximum=(int64_t(skill->maximumDamage*256.f)+itemMaximum)*frames/256;
            }
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
    if (entry->aura) return {};
    if (entry->basicAction != BasicSkillAction::None) {
        const bool thrown = entry->basicAction == BasicSkillAction::Throw || entry->basicAction == BasicSkillAction::LeftHandThrow;
        const bool leftHand = entry->basicAction == BasicSkillAction::LeftHandThrow ||
                              entry->basicAction == BasicSkillAction::LeftHandSwing;
        return weaponStats(context, thrown, leftHand);
    }
    if (entry->spell && !entry->passive) {
        if (rank < 1) return {};
        if (rank > 255) return {"?", ""};
        const auto cast = resolveSkill(entry->spell->rules(), {rank, context.learned, context.fireMastery,
            context.lightningMastery, context.attributes.combat.coldSkillDamagePercent, context.attributes.combat.poisonSkillDamagePercent});
        if (cast.curse || cast.amazonMagic || cast.summon) return {};
        if (cast.weapon) return weaponStats(context, cast.weapon->thrown, false, &cast);
        if (cast.effect == SkillBehavior::Teleport || cast.effect == SkillBehavior::StaticField ||
            cast.effect == SkillBehavior::FrozenArmor || cast.effect == SkillBehavior::EnergyShield) return {};
        if (!context.spellDamageKnown) return {"?", ""};
        if (cast.effect == SkillBehavior::Inferno)
            return {damageText(int64_t(cast.minimumDamage * 25), int64_t(cast.maximumDamage * 25)) + "/s", ""};
        return {damageText(int64_t(cast.minimumDamage), int64_t(cast.maximumDamage)), ""};
    }
    return {};
}

CharacterActionDisplay describeCharacterAction(const CharacterDisplayContext &context,const SkillDescriptionCatalog &catalog,
    const SkillDescriptionInput &input,const SkillRecord &entry,bool available) {
    if(!available || entry.passive) return {};
    const auto found=catalog.skills.find(entry.id);
    if(found==catalog.skills.end() || !found->second.hasDescription) return {};
    const auto &source=found->second;const auto &description=source.description;
    if(!description.damageProgram && !description.attackProgram) return {};
    // The original character panel dispatches SkillDesc.DescDam/DescAtt, not
    // server execution availability. Generic spell programs share the same
    // fixed-point calculator as the picker/tree, including poison totals.
    CharacterActionDisplay result;
    SkillDescriptionValues values(catalog,input,source);
    if(description.damageProgram==5 || description.damageProgram==8 || description.damageProgram==9 || description.damageProgram==12) {
        auto physical=values.physicalDamage(true),elemental=values.elementalDamage();
        if(description.damageProgram==9 && !description.missiles[0].empty())
            elemental=values.missileElementalDamage(0,75,DescriptionMastery::Include);
        if(elemental.minimum && elemental.maximum && values.element()=="pois") {
            const auto frames=values.parameter("edln");
            if(frames) elemental=values.elementalDamage(*frames);
            else elemental={};
        }
        if(description.damageProgram==8) {
            const auto multiplier=values.formula(description.damageCalculations[0]),divisor=values.formula(description.damageCalculations[1]);
            if(elemental.minimum && elemental.maximum && multiplier && divisor) {
                elemental=values.elementalDamage(std::max(1,*multiplier)*25);
                if(elemental.minimum) elemental.minimum=*elemental.minimum/std::max(1,*divisor);
                if(elemental.maximum) elemental.maximum=*elemental.maximum/std::max(1,*divisor);
            } else elemental={};
        } else if(description.damageProgram==9 && description.missiles[0].empty()) elemental=values.elementalDamage(75);
        if(description.damageProgram==12 && elemental.minimum && elemental.maximum) {
            const auto factor=source.number("param1");
            if(factor) {
                const int percent=context.concentrationDamagePercent * *factor / 8;
                elemental.minimum=int(int64_t(*elemental.minimum)*(100+percent)/100);
                elemental.maximum=int(int64_t(*elemental.maximum)*(100+percent)/100);
            } else elemental={};
        }
        if(physical.minimum && physical.maximum && elemental.minimum && elemental.maximum)
            result.damage=damageText(int64_t(*physical.minimum)+*elemental.minimum,int64_t(*physical.maximum)+*elemental.maximum);
        else result.damage="?";
    } else result=describeCharacterAction(context,&entry,input.rank.value_or(0),available);
    if(!description.damageProgram) result.damage.clear();
    if(!description.attackProgram) result.attackRating.clear();
    else if(result.attackRating.empty()) {
        if(!context.weaponValuesKnown) result.attackRating="?";
        else {
            const auto &weapon=context.equipment.weapons[0];
            const auto bonus=values.parameter("toht");
            result.attackRating=bonus?std::to_string(int64_t(weapon.baseAttackRating)*std::max(0,100+weapon.attackRatingPercent+*bonus)/100):"?";
        }
    }
    return result;
}

} // namespace d2x

#include "gameplay/combat/poison.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/resolve.hpp"
#include "character_display.hpp"
#include "combat_display.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "content/skills/skill_data.hpp"
#include "gameplay/skills/amazon_magic_spec.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "content/skills/description_values.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace d2x {
namespace {
std::string damageText(int64_t minimum, int64_t maximum) {
    return std::to_string(minimum) + "-" + std::to_string(maximum);
}
std::string nativeDamageText(int64_t minimum, int64_t maximum, bool singleAllowed = false) {
    if(singleAllowed && minimum==maximum) return std::to_string(minimum);
    if(!singleAllowed && maximum<=minimum) maximum=minimum+1;
    const auto format=[](int64_t value) {
        return value>=10000?std::to_string((value+500)/1000)+"K":std::to_string(value);
    };
    return format(minimum)+'-'+format(maximum);
}
uint8_t elementColor(std::string_view element) {
    if(element=="fire") return 1;
    if(element=="ltng") return 9;
    if(element=="pois") return 2;
    if(element=="cold" || element=="frze" || element=="mag") return 3;
    return 0;
}
CharacterActionDisplay kickPanel(const KickPanelStats *stats,const SkillDescriptionCatalog &catalog,
                                 const SkillDescriptionInput &input,SkillDescriptionValues &values) {
    CharacterActionDisplay result{"?","?"};
    if(!stats) return result;
    const auto &description=values.source().description;
    const bool finishing=values.source().number("finishing").value_or(0)!=0;
    if(const auto toHit=values.parameter("toht")) {
        const int chargeRating=finishing?stats->progressiveRating:0;
        const auto percent=nativeAdd(nativeAdd(stats->attackRatingPercent,*toHit),chargeRating);
        const auto rating=nativeAdd(stats->baseAttackRating,nativeMultiplyDivide(stats->baseAttackRating,percent,100));
        result.attackRating=std::to_string(rating);
        result.attackRatingColor=stats->ratingColor?stats->ratingColor:chargeRating?3:0;
    }
    if(!input.rank || *input.rank<=0) return result;
    const bool tail=description.damageProgram==16;
    auto physical=stats->physical;
    if(!tail) {
        const auto first=values.source().number("param1"),increment=values.source().number("param2");
        if(!first || !increment) return result;
        physical.skillPercent=nativeAdd(*first,nativeMultiply(*input.rank-1,*increment));
    }
    for(const auto &charge:stats->charges) if(charge.damageKind==1) {
        const auto found=catalog.skills.find(charge.skill);
        if(found==catalog.skills.end()) return result;
        const auto first=found->second.number("param1"),increment=found->second.number("param2");
        if(!first || !increment) return result;
        const auto bonus=nativeAdd(*first,nativeMultiply(charge.rank-1,*increment));
        physical.skillPercent=nativeAdd(physical.skillPercent,nativeMultiply(charge.count,bonus));
    }
    auto [minimum,maximum]=assassinKickRange(physical);
    uint8_t color=physical.skillPercent?3:0;
    if(tail) {
        // DescDam16 scales the kick first, then reads the unit's elemental
        // stats with both weapon damage lists detached.
        const auto fire=values.parameter("clc1");
        if(!fire) return result;
        minimum=nativeAdd(minimum,nativePercentage(minimum,*fire,100));
        maximum=nativeAdd(maximum,nativePercentage(maximum,*fire,100));
        color=elementColor(values.element());
        const auto &elements=stats->elements;
        const auto add=[&](int low,int high,int mastery,uint8_t tint) {
            low=nativeAdd(low,nativePercentage(low,mastery,100));
            high=nativeAdd(high,nativePercentage(high,mastery,100));
            minimum=nativeAdd(minimum,std::min(low,high));maximum=nativeAdd(maximum,high);
            if(low || high) color=tint;
        };
        add(elements.fireMinimum,elements.fireMaximum,elements.fireSkillDamagePercent,1);
        add(elements.lightningMinimum,elements.lightningMaximum,elements.lightningSkillDamagePercent,9);
        add(elements.magicMinimum,elements.magicMaximum,elements.magicSkillDamagePercent,3);
        add(elements.coldMinimum,elements.coldMaximum,elements.coldSkillDamagePercent,3);
        if(elements.poisonMaximum) {
            const auto length=weaponPoisonLength(elements.poisonFrames,elements.poisonSources,0,elements.poisonOverrideFrames);
            const auto poison=[&](int rate) {
                const auto adjusted=nativeAdd(rate,nativePercentage(rate,elements.poisonSkillDamagePercent,100));
                return nativeMultiply(adjusted,length)>>8;
            };
            minimum=nativeAdd(minimum,poison(elements.poisonMinimum));maximum=nativeAdd(maximum,poison(elements.poisonMaximum));
            color=2;
        }
        if(maximum>0) minimum=std::min(maximum,std::max(minimum,1));
    }
    if(!tail || finishing) for(const auto &charge:stats->charges) {
        const auto found=catalog.skills.find(charge.skill);
        if(found==catalog.skills.end()) return result;
        auto chargeInput=input;chargeInput.skill=charge.skill;chargeInput.rank=charge.rank;
        SkillDescriptionValues chargeValues(catalog,chargeInput,found->second);
        const auto &row=found->second.description.chargeDamage[size_t(std::clamp(charge.count,1,3)-1)];
        const auto damage=found->second.hasDescription?DescriptionRange{chargeValues.formula(row.minimum),chargeValues.formula(row.maximum)}:
            chargeValues.elementalDamage();
        if(!damage.minimum || !damage.maximum) return result;
        minimum=nativeAdd(minimum,*damage.minimum);maximum=nativeAdd(maximum,*damage.maximum);
        if(const auto tint=elementColor(row.element)) color=tint;
    }
    result.damage=nativeDamageText(minimum,maximum);
    result.damageColor=tail && stats->damageColor?stats->damageColor:color;
    return result;
}
CharacterActionDisplay weaponStats(const CharacterDisplayContext &context, bool thrown, bool leftHand,
                                 const SkillCastSpec *skill = nullptr, int selectedWeapon = -1,
                                 SkillDescriptionValues *description = nullptr) {
    if (!context.weaponValuesKnown) return {"?", "?"};
    const auto &equipment = context.equipment;
    if(context.dualWield && !leftHand && selectedWeapon<0 && equipment.weaponCount==2) {
        auto first=weaponStats(context,thrown,false,skill,0,description);
        const auto second=weaponStats(context,thrown,false,skill,1,description);
        if(!second.damage.empty()) first.damage+='\n'+second.damage;
        if(!second.attackRating.empty()) first.attackRating+='\n'+second.attackRating;
        return first;
    }
    const auto &combat = context.attributes.combat;
    for (int index = 0; index < equipment.weaponCount; ++index) {
        if(selectedWeapon>=0 && index!=selectedWeapon) continue;
        const auto &weapon = equipment.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if ((thrown && !weapon.throwable) || (!thrown && weapon.potion)) return {};
        if (skill && !skill->weapon->smite && !skill->weapon->requiredType.empty() && std::find(weapon.types.begin(), weapon.types.end(), skill->weapon->requiredType) == weapon.types.end())
            return {};
        if (skill && skill->weapon->smite) {
            if (!equipment.shield) return {};
            // Retail DescDam10 starts with the shield and posted Holy Shield
            // range, then applies its own display formula at 305D0.
            auto percent=nativeAdd(skill->weapon->damagePercent,combat.damagePercent);
            percent=nativeAdd(percent,nativeMultiply(equipment.shieldStrengthBonus,context.attributes.strength)/100);
            percent=nativeAdd(percent,nativeMultiply(equipment.shieldDexterityBonus,context.attributes.dexterity)/100);
            percent=std::max(percent,-90);
            const auto shown=[&](int32_t base,int32_t endpointPercent) {
                return nativeAdd(base,nativeAdd(nativeMultiply(base,percent),endpointPercent)/100);
            };
            return {damageText(shown(nativeAdd(equipment.smiteMinimum,combat.smiteMinimum),combat.minimumDamagePercent),
                shown(nativeAdd(equipment.smiteMaximum,combat.smiteMaximum),combat.maximumDamagePercent)), ""};
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
        const auto mastered=[](int value,int percent){return int64_t(value)*(100+percent)/100;};
        int64_t elementalMinimum = mastered(elements.fire.minimum,context.fireMastery) +
            mastered(elements.lightning.minimum,context.lightningMastery) +
            mastered(elements.cold.minimum,combat.coldSkillDamagePercent) + mastered(elements.magic.minimum,combat.magicSkillDamagePercent);
        int64_t elementalMaximum = mastered(elements.fire.maximum,context.fireMastery) +
            mastered(elements.lightning.maximum,context.lightningMastery) +
            mastered(elements.cold.maximum,combat.coldSkillDamagePercent) + mastered(elements.magic.maximum,combat.magicSkillDamagePercent);
        if (skill && skill->effect == SkillBehavior::Vengeance) {
            const auto low=weapon.nativeModifiers?std::max(1,weapon.rawMeleeMinimum):weapon.meleeBaseMinimum/256;
            const auto high=weapon.nativeModifiers?std::max({2,low,weapon.rawMeleeMaximum}):weapon.meleeBaseMaximum/256;
            // resolveSkill has already applied each element's mastery to its percentage.
            for (const auto percent:skill->weapon->elementPercent) {
                elementalMinimum+=nativePercentage(low,percent,100);
                elementalMaximum+=nativePercentage(high,percent,100);
            }
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
        const auto poisonOverride=nativeAdd(combat.poisonOverrideFrames,own.poisonOverrideFrames);
        const auto itemFrames=weaponPoisonLength(int64_t(combat.poisonFrames)+own.poisonFrames,combat.poisonSources+own.poisonSources,0,poisonOverride);
        int64_t poisonMinimum=itemMinimum*itemFrames/256,poisonMaximum=itemMaximum*itemFrames/256;
        if(skill && skill->poisonDuration>0) {
            const auto skillFrames=int64_t(skill->poisonDuration*25.f+.001f);
            if(skill->weapon->spear) {
                poisonMinimum=int64_t(skill->minimumDamage*256.f)*skillFrames/256;
                poisonMaximum=int64_t(skill->maximumDamage*256.f)*skillFrames/256;
            } else {
                const auto frames=weaponPoisonLength(int64_t(combat.poisonFrames)+own.poisonFrames,combat.poisonSources+own.poisonSources,skillFrames,poisonOverride);
                poisonMinimum=(int64_t(skill->minimumDamage*256.f)+itemMinimum)*frames/256;
                poisonMaximum=(int64_t(skill->maximumDamage*256.f)+itemMaximum)*frames/256;
            }
        }
        const int rating = int(std::clamp<int64_t>(int64_t(weapon.baseAttackRating) *
            std::max(0, 100 + weapon.attackRatingPercent + (skill ? skill->weapon->attackRating : 0)) / 100,
            0, std::numeric_limits<int>::max()));
        if(description && description->source().description.damageProgram==14 && skill && !skill->pd2 &&
           skill->druid && skill->druid->program==DruidProgram::Hunger) {
            const auto penalty=description->source().number("param5");
            if(!penalty) return {"?",std::to_string(rating)};
            const auto physical=[&](int fixed) {
                const int whole=fixed/256;
                return nativeAdd(whole,nativeMultiply(whole,*penalty)/100);
            };
            return {nativeDamageText(physical(weapon.minimum)+elementalMinimum+poisonMinimum,
                physical(weapon.maximum)+elementalMaximum+poisonMaximum),std::to_string(rating)};
        }
        const int flat=skill?skill->weapon->physicalFlat:0;
        int64_t skillMinimum = flat+(skill && skill->poisonDuration == 0 ? int64_t(skill->minimumDamage) : 0);
        int64_t skillMaximum = flat+(skill && skill->poisonDuration == 0 ? int64_t(skill->maximumDamage) : 0);
        const auto *bow=skill?skill->weapon->bow.get():nullptr;
        if(description && !bow && weapon.nativeModifiers && description->source().description.damageProgram==7) {
            const auto &display=description->source().description;
            const auto bonus=description->formula(display.damageCalculations[0]);
            const auto extra=description->formula(display.damageCalculations[1]);
            const auto source=description->source().number("srcdam");
            const auto physical=description->physicalDamage(),elemental=description->elementalDamage();
            if(!bonus || !extra || !source || !physical.minimum || !physical.maximum || !elemental.minimum || !elemental.maximum)
                return {"?",std::to_string(rating)};
            const auto damagePercent=std::max(-90,nativeAdd(weapon.damagePercent,*bonus));
            const auto endpoint=[&](int base,int endpointPercent,int64_t itemElements,int64_t itemPoison,int skillPhysical,int skillElement) {
                const auto percent=nativeAdd(nativeAdd(100,damagePercent),endpointPercent);
                const auto weaponDamage=nativeAdd(nativeAdd(nativeMultiply(base,percent)/100,own.normalDamage),*extra);
                return (weaponDamage+itemElements+itemPoison)* *source/128+skillPhysical+skillElement;
            };
            const auto baseMinimum=std::max(1,nativeAdd(weapon.rawMeleeMinimum,weapon.item?0:1));
            const auto baseMaximum=std::max(2,nativeAdd(weapon.rawMeleeMaximum,weapon.item?0:2));
            return {nativeDamageText(endpoint(baseMinimum,weapon.minimumDamagePercent,elementalMinimum,poisonMinimum,*physical.minimum,*elemental.minimum),
                endpoint(baseMaximum,weapon.maximumDamagePercent,elementalMaximum,poisonMaximum,*physical.maximum,*elemental.maximum)),std::to_string(rating)};
        }
        if (description && (bow || description->source().description.damageProgram==5 ||
            (skill && skill->weapon->spear && description->source().description.damageProgram==17) ||
            (thrown && (description->source().description.damageProgram==21 || description->source().description.damageProgram==22)))) {
            const auto &display = description->source().description;
            const auto physical = description->physicalDamage();
            const auto element=description->element();
            auto elemental = description->elementalDamage();
            if(display.damageProgram==22 && element=="pois") {
                const auto length=description->parameter("edln");
                elemental=length?description->elementalDamage(*length):DescriptionRange{};
            }
            const auto attackRating = bow && bow->guided ? "" : std::to_string(rating);
            const auto sourceDamage=display.damageProgram==17?DescriptionNumber{128}:description->source().number("srcdam");
            DescriptionNumber bonus = 0;
            if (display.damageProgram == 7 && display.damageCalculations[0])
                bonus = description->formula(display.damageCalculations[0]);
            if (!sourceDamage || !bonus || !physical.minimum || !physical.maximum || !elemental.minimum || !elemental.maximum)
                return {"?", attackRating};

            // DescDam5/6/7 scale the displayed weapon total before adding
            // the separate skill curves. DescDam7 supplies its own ED.
            const int64_t percent = std::max<int64_t>(0, 100LL + weapon.projectileDamagePercent + *bonus);
            const auto baseMinimum=bow?weapon.projectileMinimum * percent / 100:thrown?weapon.throwMinimum:weapon.minimum;
            const auto baseMaximum=bow?weapon.projectileMaximum * percent / 100:thrown?weapon.throwMaximum:weapon.maximum;
            if(display.damageProgram==21) {
                const auto minimum=(baseMinimum/256+elements.lightning.minimum)* *sourceDamage/128+*elemental.minimum;
                const auto maximum=(baseMaximum/256+elements.lightning.maximum)* *sourceDamage/128+*elemental.maximum;
                return {nativeDamageText(minimum,maximum),attackRating,9};
            }
            const bool separate=display.damageProgram==6 || display.damageProgram==17 || display.damageProgram==22;
            const int64_t weaponMinimum = (baseMinimum / 256 + elementalMinimum + (separate?itemMinimum*itemFrames/256:poisonMinimum)) * *sourceDamage / 128;
            const int64_t weaponMaximum = (baseMaximum / 256 + elementalMaximum + (separate?itemMaximum*itemFrames/256:poisonMaximum)) * *sourceDamage / 128;
            const int64_t addedMinimum = int64_t(*physical.minimum) + *elemental.minimum;
            const int64_t addedMaximum = int64_t(*physical.maximum) + *elemental.maximum;
            // D2Client 2C830 selects the original PL2 text color from EType.
            const uint8_t color=element=="fire"?1:element=="ltng"?9:element=="pois"?2:element=="cold" || element=="frze"?3:0;
            if (separate) {
                const auto &matching = element == "fire" ? elements.fire : elements.cold;
                const auto burstMinimum=display.damageProgram==6?addedMinimum+matching.minimum:*elemental.minimum;
                const auto burstMaximum=display.damageProgram==6?addedMaximum+matching.maximum:*elemental.maximum;
                if (!weaponMinimum && !weaponMaximum)
                    return {nativeDamageText(burstMinimum,burstMaximum),attackRating,color};
                if (burstMinimum || burstMaximum)
                    return {nativeDamageText(weaponMinimum,weaponMaximum,true)+'\n'+nativeDamageText(burstMinimum,burstMaximum,true),
                        attackRating,0,color};
                return {nativeDamageText(weaponMinimum,weaponMaximum),attackRating};
            }
            return {nativeDamageText(weaponMinimum+addedMinimum,weaponMaximum+addedMaximum),attackRating,color};
        }
        if (bow) {
            const int percent = std::max(0, 100 + weapon.projectileDamagePercent + skillDamage);
            minimum = (int64_t(weapon.projectileMinimum) * bow->sourceDamage / 128 +
                (bow->physicalSkillDamage ? int64_t(skill->minimumDamage * 256.f) : 0)) * percent / 100;
            maximum = (int64_t(weapon.projectileMaximum) * bow->sourceDamage / 128 +
                (bow->physicalSkillDamage ? int64_t(skill->maximumDamage * 256.f) : 0)) * percent / 100;
            elementalMinimum = elementalMinimum * bow->sourceDamage / 128;
            elementalMaximum = elementalMaximum * bow->sourceDamage / 128;
            if (bow->physicalSkillDamage) skillMinimum = skillMaximum = 0;
        }
        return {damageText(int64_t(minimum) / 256 + elementalMinimum + poisonMinimum + skillMinimum,
                           int64_t(maximum) / 256 + elementalMaximum + poisonMaximum + skillMaximum), skill && skill->weapon->bow && skill->weapon->bow->guided ? "" : std::to_string(rating)};
    }
    return {};
}
} // namespace

namespace {
CharacterActionDisplay describeAction(const CharacterDisplayContext &context,
    const SkillRecord *entry, int rank, bool available, SkillDescriptionValues *description = nullptr) {
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
        auto cast = resolveSkill(entry->spell->rules(), {rank, context.learned, context.fireMastery,
            context.lightningMastery, context.attributes.combat.coldSkillDamagePercent, context.attributes.combat.poisonSkillDamagePercent});
        if(cast.assassin && cast.assassin->program==AssassinProgram::DragonClaw) {
            const auto percent=description?(description->source().description.damageProgram==19 && !cast.pd2?
                description->formula(description->source().description.damageCalculations[0]):
                description->parameter("clc1")):DescriptionNumber{};
            if(!percent) return {"?","?"};
            cast.weapon->damagePercent=*percent;
        }
        if (cast.curse || cast.amazonMagic || cast.summon) return {};
        if (cast.weapon) return weaponStats(context, cast.weapon->thrown, false, &cast, -1, description);
        if (cast.effect == SkillBehavior::Teleport || cast.effect == SkillBehavior::StaticField ||
            cast.effect == SkillBehavior::FrozenArmor || cast.effect == SkillBehavior::EnergyShield) return {};
        if (!context.spellDamageKnown) return {"?", ""};
        if (cast.effect == SkillBehavior::Inferno)
            return {damageText(int64_t(cast.minimumDamage * 25), int64_t(cast.maximumDamage * 25)) + "/s", ""};
        return {damageText(int64_t(cast.minimumDamage), int64_t(cast.maximumDamage)), ""};
    }
    return {};
}
}
CharacterActionDisplay describeCharacterAction(const CharacterDisplayContext &context,
    const SkillRecord *entry, int rank, bool available) {
    return describeAction(context,entry,rank,available);
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
    if((description.damageProgram==15 || description.damageProgram==16) && description.attackProgram==5 &&
       !(entry.spell && entry.spell->pd2))
        return kickPanel(context.kick,catalog,input,values);
    const auto *weapon=entry.spell && entry.spell->weapon?&*entry.spell->weapon:nullptr;
    const bool fireClaws=entry.spell && !entry.spell->pd2 && entry.spell->druid && entry.spell->druid->program==DruidProgram::FireClaws;
    const bool nativeWeapon=weapon && (weapon->bow || fireClaws || (weapon->spear && weapon->spear->kind==SpearSkillSpec::Kind::Power));
    if((description.damageProgram==5 && !nativeWeapon) || description.damageProgram==8 || description.damageProgram==9 || description.damageProgram==12) {
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
        if(description.damageProgram==5) {
            const auto element=values.element();
            result.damageColor=element=="fire"?1:element=="ltng"?9:element=="pois"?2:element=="cold" || element=="frze"?3:0;
        }
    } else {
        auto weaponContext=context;
        weaponContext.dualWield=context.dualWield && (description.damageProgram==1 || description.damageProgram==19);
        result=describeAction(weaponContext,&entry,input.rank.value_or(0),available,&values);
    }
    if(!description.damageProgram) result.damage.clear();
    else if(result.damage.empty() && !entry.aura && !entry.spell && entry.basicAction==BasicSkillAction::None)
        result.damage="?";
    if(!description.attackProgram) result.attackRating.clear();
    else if(result.attackRating.empty()) {
        if(!context.weaponValuesKnown) result.attackRating="?";
        else {
            const auto bonus=values.parameter("toht");
            const int count=context.dualWield && description.attackProgram==2?context.equipment.weaponCount:1;
            for(int index=0;index<count;++index) {
                const auto &weapon=context.equipment.weapons[index];
                if(index) result.attackRating+='\n';
                result.attackRating+=bonus?std::to_string(int64_t(weapon.baseAttackRating)*std::max(0,100+weapon.attackRatingPercent+*bonus)/100):"?";
            }
        }
    }
    return result;
}

} // namespace d2x
